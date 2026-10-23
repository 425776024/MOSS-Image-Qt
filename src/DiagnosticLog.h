#pragma once

#include <QJsonObject>
#include <QMutex>
#include <QString>
#include <QtLogging>

// An append-only cache: writing refreshes recency; exporting never pins old logs.
// All processes share the same byte/file budget under a short-lived file lock.
class DiagnosticLog {
public:
    static constexpr qint64 SegmentBytes = 2 * 1024 * 1024;
    static constexpr int SegmentCount = 8;
    explicit DiagnosticLog(QString directory, qint64 segmentBytes = SegmentBytes,
                           int segmentCount = SegmentCount);
    static DiagnosticLog &instance();
    bool record(QString level, QString event, QJsonObject fields = {});
    bool exportTo(const QString &destination, QString *error);
    QString error() const;
    static QString redact(QString text);
    static QJsonObject systemInfo();
    static void installQtHandler();
    static void restoreQtHandler();
private:
    QString m_directory, m_current, m_session, m_error;
    qint64 m_segmentBytes;
    int m_segmentCount;
    mutable QMutex m_mutex;
};
