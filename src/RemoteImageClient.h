#pragma once
#include <QObject>
#include <QJsonObject>
#include <QLocalServer>
#include <QProcess>
#include <QTimer>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

class RemoteImageClient : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(QString host READ host NOTIFY changed)
    Q_PROPERTY(QString user READ user NOTIFY changed)
    Q_PROPERTY(int port READ port NOTIFY changed)
    Q_PROPERTY(QString archiveRoot READ archiveRoot NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(int pendingCount READ pendingCount NOTIFY changed)
public:
    explicit RemoteImageClient(QObject *parent=nullptr);
    ~RemoteImageClient() override;
    bool available()const{return false;}
    QString host()const{return m_config["host"].toString();}
    QString user()const{return m_config["user"].toString();}
    int port()const{return m_config["port"].toInt(22);}
    QString archiveRoot()const;
    QStringList archiveRoots()const;
    QString status()const{return m_status;}
    QString error()const{return m_error;}
    bool ready()const{return m_ready;}
    bool busy()const{return m_process.state()!=QProcess::NotRunning;}
    bool active()const{return !m_directory.isEmpty();}
    bool modelReady(QString quant)const{return m_ready&&m_modelsReady[quant].toBool();}
    int pendingCount()const;
    QString latestPending()const;
    Q_INVOKABLE bool configure(QString host,QString user,int port);
    Q_INVOKABLE void connectServer(QString password={});
    Q_INVOKABLE void chooseArchive(QUrl directory);
    Q_INVOKABLE void openArchive();
    bool start(QString directory,QString kind,QString quant,const QStringList &engineArgs);
    bool resume(QString directory);
    void cancel();
    static int askpass();
signals:
    void changed();
    void archivesChanged();
    void output(QByteArray bytes);
    void phase(QString text);
    void completed(int exitCode,bool cancelled,bool recoverable);
private:
    void request(QString action,QJsonObject payload={});
    void response(int exitCode,QProcess::ExitStatus exitStatus);
    void transportFailure(QString error);
    void poll();
    void finish(int code,bool cancelled,bool recoverable);
    void clearPassword();
    bool saveConfig();
    QString knownHosts()const;
    QJsonObject activeConfig()const;
    QStringList sshArguments(const QJsonObject &config)const;
    QJsonObject m_config,m_job,m_modelsReady;
    QString m_configPath,m_base,m_directory,m_action,m_status="尚未连接",m_error;
    QProcess m_process;
    QTimer m_poll,m_timeout;
    QLocalServer m_auth;
    QByteArray m_password,m_payload,m_stdout,m_stderr;
    qint64 m_offset=0;
    int m_failures=0,m_authRequests=0;
    bool m_ready=false,m_cancel=false;
};
