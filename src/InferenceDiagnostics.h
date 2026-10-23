#pragma once
#include "DiagnosticLog.h"
#include <QElapsedTimer>
#include <QJsonArray>

// One instance per sequential worker chain. Parsing never drives inference.
class InferenceDiagnostics {
public:
    explicit InferenceDiagnostics(DiagnosticLog *log = nullptr);
    void begin(QString kind, QString worker, bool remote, const QJsonObject &parameters);
    void consume(const QByteArray &bytes);
    void finish(bool success, bool cancelled, int exitCode, bool crashed,
                QString error, const QJsonObject &result = {});
    void processError(int code, QString message);
    void heartbeat();
    QJsonObject summary() const;
private:
    void line(const QByteArray &bytes);
    void stage(QString name);
    void event(QString level, QString name, QJsonObject data = {});
    DiagnosticLog *m_log;
    QString m_id, m_stage;
    QElapsedTimer m_elapsed;
    qint64 m_stageStart = 0, m_lastProgress = -1000;
    QByteArray m_pending;
    bool m_active = false, m_discardLine = false;
    QJsonObject m_backends;
};
