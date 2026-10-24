#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSaveFile>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <functional>
#include <memory>

class AppUpdate final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool appleStore READ appleStore CONSTANT)
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(QVariantMap latest READ latest NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    Q_PROPERTY(bool canInstall READ canInstall NOTIFY changed)
public:
    explicit AppUpdate(QObject *parent=nullptr,QNetworkAccessManager *network=nullptr,QString installation={});
    bool appleStore() const;
    QString currentVersion() const;
    QString state() const { return m_state; }
    QVariantMap latest() const { return m_latest.toVariantMap(); }
    double progress() const { return m_progress; }
    bool canInstall() const;
    Q_INVOKABLE void check();
    Q_INVOKABLE void install();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void openStore(bool testFlight=false);
    static QJsonObject selectRelease(const QJsonArray &,const QString &platform);
    static bool newer(const QJsonObject &,const QString &version,const QString &commit,const QString &builtAt);
    static QJsonObject feedRelease(const QByteArray &,const QString &platform);
    static QJsonObject checksumAsset(const QByteArray &,const QString &platform,const QString &tag);
    static QUrl endpoint(bool store,const QString &country={});
    static QString updateRoot();
    static bool acknowledgeLaunch(const QString &token);
    std::function<bool()> canRestart;
signals:
    void changed();
    void restartRequested();
private:
    void setState(const QString &);
    void fail(const QString &code="error");
    void fetch(const QUrl &,std::function<void(QByteArray)>,std::function<void()> failed={});
    void checkCommunity(int page=1);
    void checkFeed();
    void finishCommunityCheck();
    void prepareInstall();
    void run(const QString &,const QStringList &,std::function<void(bool,QByteArray)>);
    void launchHelper(const QString &staged={});
    QString installRoot() const;
    QString m_state="idle",m_work,m_packagePath,m_token,m_installation;
    QJsonObject m_latest;
    QNetworkAccessManager *m_network;
    QPointer<QNetworkReply> m_reply;
    std::unique_ptr<QSaveFile> m_package;
    QCryptographicHash m_hash{QCryptographicHash::Sha256};
    qint64 m_received=0;
    double m_progress=0;
};
