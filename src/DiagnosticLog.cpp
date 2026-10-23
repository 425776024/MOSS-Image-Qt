#include "DiagnosticLog.h"
#include "UserSelectedFileExport.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSysInfo>
#include <QThread>
#include <QUuid>
#include <cstdio>
#include <algorithm>

namespace {
QtMessageHandler previousHandler = nullptr;
bool handlerInstalled = false;
QJsonValue sanitized(const QJsonValue &value) {
    if (value.isString()) return DiagnosticLog::redact(value.toString());
    if (value.isArray()) {
        QJsonArray out; for (const auto &item : value.toArray()) out.append(sanitized(item)); return out;
    }
    if (value.isObject()) {
        QJsonObject out;
        const auto object = value.toObject();
        static const QRegularExpression secret("prompt|password|token|authorization|secret|referenceText|description|^text$", QRegularExpression::CaseInsensitiveOption);
        for (auto it = object.begin(); it != object.end(); ++it)
            out[it.key()] = secret.match(it.key()).hasMatch() ? QJsonValue("<redacted>") : sanitized(it.value());
        return out;
    }
    return value;
}
void qtMessage(QtMsgType type, const QMessageLogContext &context, const QString &message) {
    const auto level = type == QtFatalMsg ? "fatal" : type == QtCriticalMsg ? "error" : type == QtWarningMsg ? "warning" : type == QtDebugMsg ? "debug" : "info";
    DiagnosticLog::instance().record(level, "qt", {{"category", QString::fromUtf8(context.category ? context.category : "default")}, {"message", message}});
    if (previousHandler) previousHandler(type, context, message);
    else { const auto bytes = qFormatLogMessage(type, context, message).toLocal8Bit(); std::fprintf(stderr, "%s\n", bytes.constData()); }
}
}

DiagnosticLog::DiagnosticLog(QString directory, qint64 segmentBytes, int segmentCount)
    : m_directory(std::move(directory)), m_session(QUuid::createUuid().toString(QUuid::Id128)),
      m_segmentBytes(qMax<qint64>(1024, segmentBytes)), m_segmentCount(qMax(1, segmentCount)) {}

DiagnosticLog &DiagnosticLog::instance() {
    static DiagnosticLog log(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/diagnostics");
    return log;
}

QString DiagnosticLog::redact(QString text) {
    text.replace(QRegularExpression("\x1b\\[[0-9;]*[A-Za-z]"), "");
    text.replace(QDir::homePath(), "<home>", Qt::CaseInsensitive);
    // Strip query credentials, bearer tokens and absolute user paths before persistence.
    text.replace(QRegularExpression(R"((?i)(bearer\s+)\S+)"), "\\1<redacted>");
    text.replace(QRegularExpression(R"((?i)((?:password|token|api[_-]?key|secret|authorization)\s*[=:]\s*)[^\s,;]+)"), "\\1<redacted>");
    text.replace(QRegularExpression(R"((?:https?|ssh)://[^\s"'<>]+)"), "<url>");
    text.replace(QRegularExpression(R"((?:[A-Za-z]:[\\/]|/Users/|/home/|/private/|/tmp/|/var/|<home>/)[^\r\n"'<>]*)"), "<path>");
    return text.left(8192);
}

QJsonObject DiagnosticLog::systemInfo() {
    return {{"appVersion", QCoreApplication::applicationVersion()}, {"qtVersion", qVersion()},
        {"os", QSysInfo::prettyProductName()}, {"kernel", QSysInfo::kernelVersion()},
        {"cpuArchitecture", QSysInfo::currentCpuArchitecture()}, {"buildArchitecture", QSysInfo::buildCpuArchitecture()},
        {"logicalCpuCount", QThread::idealThreadCount()},
#ifdef MOSS_APP_STORE
        {"edition", "app-store"},
#else
        {"edition", "public"},
#endif
        {"gpuUtilization", "not_measured"}};
}

bool DiagnosticLog::record(QString level, QString event, QJsonObject fields) {
    QMutexLocker guard(&m_mutex);
    auto fail = [this](QString error) { m_error = std::move(error); return false; };
    if (!QDir().mkpath(m_directory)) return fail("Cannot create diagnostic directory");
    QLockFile lock(m_directory + "/writer.lock");
    if (!lock.tryLock(50)) return fail("Diagnostic directory is busy");
    QJsonObject entry{{"time", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {"session", m_session}, {"level", level}, {"event", event}, {"data", sanitized(fields)}};
    QByteArray bytes = QJsonDocument(entry).toJson(QJsonDocument::Compact) + '\n';
    if (bytes.size() > qMin<qint64>(32768, m_segmentBytes)) {
        entry["data"] = QJsonObject{{"truncated", true}, {"originalBytes", bytes.size()}};
        bytes = QJsonDocument(entry).toJson(QJsonDocument::Compact) + '\n';
    }
    auto files = QDir(m_directory).entryInfoList({"diagnostic-*.jsonl"}, QDir::Files | QDir::NoSymLinks, QDir::Time | QDir::Reversed);
    if (m_current.isEmpty() || !QFileInfo::exists(m_current) || QFileInfo(m_current).size() + bytes.size() > m_segmentBytes) {
        // Reuse the newest partial segment after restart. Otherwise eight short
        // launches could evict useful history despite having megabytes free.
        if (!files.isEmpty() && files.last().size() + bytes.size() <= m_segmentBytes)
            m_current = files.last().absoluteFilePath();
        else
            m_current = m_directory + "/diagnostic-" + QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmsszzz") + "-" + QUuid::createUuid().toString(QUuid::Id128) + ".jsonl";
    }
    qint64 total = 0; for (const auto &file : files) total += file.size();
    int count = files.size() + (QFileInfo::exists(m_current) ? 0 : 1);
    for (const auto &file : files) {
        if (count <= m_segmentCount && total + bytes.size() <= m_segmentBytes * m_segmentCount) break;
        if (file.absoluteFilePath() == m_current) continue;
        if (!QFile::remove(file.absoluteFilePath())) return fail("Cannot remove expired diagnostic segment");
        total -= file.size(); --count;
    }
    QFile file(m_current);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append)) return fail(file.errorString());
    if (file.write(bytes) != bytes.size() || !file.flush()) return fail(file.errorString());
    m_error.clear();
    return true;
}

QString DiagnosticLog::error() const { QMutexLocker guard(&m_mutex); return m_error; }

bool DiagnosticLog::exportTo(const QString &destination, QString *error) {
    QByteArray snapshot;
    {
        QMutexLocker guard(&m_mutex);
        if (!QDir().mkpath(m_directory)) { *error = "Cannot read diagnostic directory"; return false; }
        const auto parent = QFileInfo(destination).absoluteDir().canonicalPath();
        const auto root = QDir(m_directory).canonicalPath();
        if (parent == root || parent.startsWith(root + '/') || QFileInfo(destination).isSymLink()) {
            *error = "Choose a destination outside the diagnostic cache"; return false;
        }
        QLockFile lock(m_directory + "/writer.lock");
        if (!lock.tryLock(50)) { *error = "Diagnostic directory is busy; please retry"; return false; }
        snapshot = QJsonDocument(QJsonObject{{"event", "diagnostic-export"}, {"schemaVersion", 1},
            {"time", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}, {"system", systemInfo()},
            {"retentionBytes", m_segmentBytes * m_segmentCount}, {"recordingError", m_error},
            {"note", "Backend selection is not GPU utilization. Unknown means no worker evidence. Old segments expire by last write; exports do not refresh recency."}}).toJson(QJsonDocument::Compact) + '\n';
        auto files = QDir(m_directory).entryInfoList({"diagnostic-*.jsonl"}, QDir::Files | QDir::NoSymLinks, QDir::Time);
        // Bound snapshots even if an older build left more/larger cache files.
        files = files.mid(0, m_segmentCount);
        std::reverse(files.begin(), files.end());
        for (const auto &info : files) {
            QFile file(info.absoluteFilePath());
            if (!file.open(QIODevice::ReadOnly)) { *error = file.errorString(); return false; }
            QByteArray bytes=file.read(m_segmentBytes);
            if(!bytes.endsWith('\n'))bytes.truncate(bytes.lastIndexOf('\n')+1);
            snapshot += bytes;
        }
    }
    return writeToUserSelectedFile(snapshot, destination, error);
}

void DiagnosticLog::installQtHandler() {
    if (handlerInstalled) return;
    instance().record("info", "application-start", systemInfo());
    previousHandler = qInstallMessageHandler(qtMessage); handlerInstalled = true;
}
void DiagnosticLog::restoreQtHandler() {
    if (!handlerInstalled) return;
    qInstallMessageHandler(previousHandler); handlerInstalled = false;
    instance().record("info", "application-stop");
}
