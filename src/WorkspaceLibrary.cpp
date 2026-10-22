#include "WorkspaceLibrary.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSettings>
#include <QLocale>
#include <QDesktopServices>
#include <QUuid>
#include <QDateTime>
#include <QTextDocument>
#include <QTextBlock>
#include <QTextCursor>

WorkspaceLibrary::WorkspaceLibrary(QString outputRoot, QObject *parent)
    : QObject(parent), m_root(std::move(outputRoot) + "/library") {
    QSettings settings;
    m_appearance = settings.value("home/appearance", "light").toString();
    m_theme = settings.value("home/theme", "blue").toString();
    m_languagePreference = settings.value("home/language", "system").toString();
    if (!QStringList{"system", "zh", "en"}.contains(m_languagePreference)) m_languagePreference = "system";
    m_language = m_languagePreference == "system" ? resolveLanguage(QLocale::system().uiLanguages()) : m_languagePreference;
    QFile file(m_root + "/catalog.json");
    if (file.open(QIODevice::ReadOnly)) {
        const auto json = QJsonDocument::fromJson(file.readAll()).object();
        m_avatars = json["avatars"].toArray().toVariantList();
        m_archived = json["archived"].toArray().toVariantList();
    }
}
void WorkspaceLibrary::setAppearanceMode(QString value) {
    if (!QStringList{"system", "light", "dark"}.contains(value) || value == m_appearance) return;
    QSettings().setValue("home/appearance", value); m_appearance = value; emit preferencesChanged();
}
void WorkspaceLibrary::setThemePreset(QString value) {
    if (!QStringList{"dark", "graphite", "teal", "blue"}.contains(value) || value == m_theme) return;
    QSettings().setValue("home/theme", value); m_theme = value; emit preferencesChanged();
}
QString WorkspaceLibrary::resolveLanguage(const QStringList &uiLanguages) {
    for (QString locale : uiLanguages) {
        locale.replace('_', '-');
        const auto language = locale.section('-', 0, 0).toLower();
        if (language == "zh" || language == "en") return language;
    }
    return "en";
}
void WorkspaceLibrary::setLanguage(QString value) {
    if (!QStringList{"system", "zh", "en"}.contains(value) || value == m_languagePreference) return;
    QSettings().setValue("home/language", value);
    m_languagePreference = value;
    m_language = value == "system" ? resolveLanguage(QLocale::system().uiLanguages()) : value;
    emit preferencesChanged();
}
void WorkspaceLibrary::setSessionLanguage(QString value) {
    if (value != "en" && value != "zh") return;
    m_language = value;
    emit preferencesChanged();
}
bool WorkspaceLibrary::save(const QVariantList &avatars, const QVariantList &archived) const {
    if (!QDir().mkpath(m_root)) return false;
    QSaveFile file(m_root + "/catalog.json");
    const auto bytes = QJsonDocument(QJsonObject{{"version", 1},
        {"avatars", QJsonArray::fromVariantList(avatars)}, {"archived", QJsonArray::fromVariantList(archived)}}).toJson();
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
QString WorkspaceLibrary::importAvatar(QUrl source, QString name,QVariantMap generationRecipe) {
    const auto path = source.toLocalFile();
    QImageReader reader(path);
    if (!source.isLocalFile() || !reader.canRead()) return "请选择可读取的 PNG、JPEG 或 WebP 人像图片";
    if (name.trimmed().isEmpty() || name.trimmed().size() > 120) return "请输入 1–120 字的形象名称";
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly) || input.size() > 50 * 1024 * 1024) return "请选择 50 MB 以内的图片";
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto directory = m_root + "/avatars/" + id;
    if (!QDir().mkpath(directory)) return "无法创建形象目录";
    const auto destination = directory + "/source." + QString::fromLatin1(reader.format()).toLower();
    QSaveFile output(destination);
    const auto bytes = input.readAll();
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) {
        QDir(directory).removeRecursively(); return "无法保存形象图片";
    }
    auto list = m_avatars;
    QVariantMap row{{"id", id}, {"name", name.trimmed()}, {"url", QUrl::fromLocalFile(destination).toString()},
        {"created", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}, {"builtin", false}, {"kind", "image"}};
    if(!generationRecipe.isEmpty()){row["synthetic"]=true;row["generationRecipe"]=generationRecipe;}
    list.prepend(row);
    if (!save(list, m_archived)) { QDir(directory).removeRecursively(); return "无法保存形象库"; }
    m_avatars = list; emit avatarsChanged(); return {};
}
QString WorkspaceLibrary::renameAvatar(QString id, QString name) {
    name = name.trimmed();
    if (name.isEmpty() || name.size() > 120) return "请输入 1–120 字的形象名称";
    auto list = m_avatars;
    for (auto &value : list) {
        auto row = value.toMap(); if (row["id"] != id) continue;
        row["name"] = name; value = row;
        if (!save(list, m_archived)) return "无法保存形象库";
        m_avatars = list; emit avatarsChanged(); return {};
    }
    return "形象不存在";
}
QString WorkspaceLibrary::removeAvatar(QString id) {
    auto list = m_avatars, archived = m_archived;
    for (int i = 0; i < list.size(); ++i) {
        if (list[i].toMap()["id"] != id) continue;
        archived.append(list.takeAt(i));
        if (!save(list, archived)) return "无法保存形象库";
        m_avatars = list; m_archived = archived; emit avatarsChanged(); return {};
    }
    return "形象不存在";
}
QString WorkspaceLibrary::restoreAvatar() {
    if (m_archived.isEmpty()) return {};
    auto list = m_avatars, archived = m_archived; list.prepend(archived.takeLast());
    if (!save(list, archived)) return "无法恢复形象";
    m_avatars = list; m_archived = archived; emit avatarsChanged(); return {};
}
void WorkspaceLibrary::openDirectory(QString path) {
    QDir().mkpath(path); QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}
QString WorkspaceLibrary::notices() const {
    QFile file(":/home/THIRD_PARTY.md");
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}
QString WorkspaceLibrary::noticesHtml(const QColor &linkColor) const {
    QTextDocument document;
    document.setMarkdown(notices());
    // Markdown assigns an explicit default blue to anchors, overriding QML's
    // linkColor. Preserve the document while applying the active theme color.
    for (auto block = document.begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const auto fragment = it.fragment();
            if (!fragment.isValid() || !fragment.charFormat().isAnchor()) continue;
            QTextCursor cursor(&document);
            cursor.setPosition(fragment.position());
            cursor.setPosition(fragment.position() + fragment.length(), QTextCursor::KeepAnchor);
            QTextCharFormat color;
            color.setForeground(linkColor);
            cursor.mergeCharFormat(color);
        }
    }
    return document.toHtml();
}
