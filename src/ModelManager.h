#pragma once
#include <QObject>
#include <QJsonObject>
#include <QVariantList>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QFile>
#include <QElapsedTimer>
#include <QLockFile>
#include <memory>
#include <QSet>
#include <QHash>

struct ModelAsset {
    QString group, path, repo, revision, file, sha256, resource;
    qint64 bytes=0;
    bool appleOnly=false;
    QString modelscopeUrl;
    qint64 downloadBytes=0;
    QString downloadSha256,conversion;
    qint64 transferBytes() const { return downloadBytes?downloadBytes:bytes; }
    QString transferSha256() const { return downloadSha256.isEmpty()?sha256:downloadSha256; }
};

// The catalog is the sole authority for downloads, deletion and release packaging.
// Only explicitly catalogued channel assets are managed.
class ModelManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString root READ root NOTIFY changed)
    Q_PROPERTY(bool rootSelectable READ rootSelectable CONSTANT)
    Q_PROPERTY(QString endpoint READ endpoint WRITE setEndpoint NOTIFY changed)
    Q_PROPERTY(QStringList sourceNames READ sourceNames CONSTANT)
    Q_PROPERTY(QStringList sourceValues READ sourceValues CONSTANT)
    Q_PROPERTY(QString imageQuant READ imageQuant WRITE setImageQuant NOTIFY changed)
    Q_PROPERTY(QVariantList groups READ groups NOTIFY changed)
    Q_PROPERTY(QVariantList resources READ resources NOTIFY changed)
    Q_PROPERTY(bool initialSetup READ initialSetup NOTIFY changed)
    Q_PROPERTY(bool sfxTermsAccepted READ sfxTermsAccepted NOTIFY changed)
    Q_PROPERTY(bool sfxTermsRequired READ sfxTermsRequired NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool locked READ locked NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    Q_PROPERTY(double partialBytes READ partialBytes NOTIFY changed)
    Q_PROPERTY(QString requestDescription READ describeRequest NOTIFY changed)
    Q_PROPERTY(QVariantMap requestInfo READ requestInfo NOTIFY changed)
    Q_PROPERTY(QStringList requested READ requested NOTIFY changed)
public:
    explicit ModelManager(QObject *parent=nullptr, QByteArray catalog={}, QUrl testTransport={});
    ~ModelManager() override;
    static QList<ModelAsset> assets();
    QString root() const { return m_root; }
    bool rootSelectable() const {
#ifdef MOSS_APP_STORE
        return false;
#else
        return true;
#endif
    }
    void setRoot(QString root);
    QStringList sourceNames() const;
    QStringList sourceValues() const;
    QString endpoint() const { return m_endpoint; }
    void setEndpoint(QString value);
    QString imageQuant() const { return m_quant; }
    void setImageQuant(QString value);
    QVariantList groups() const;
    QVariantList resources() const;
    bool initialSetup() const { return m_initialSetup; }
    Q_INVOKABLE void showInitialSetup();
    Q_INVOKABLE void setRequested(QStringList capabilities);
    bool busy() const { return m_busy; }
    bool locked() const { return m_busy||m_inference; }
    void setInferenceBusy(bool value) { m_inference=value; }
    QString status() const { return m_status; }
    QString error() const { return m_error; }
    double progress() const { return m_progress; }
    double partialBytes() const;
    QStringList requested() const { return m_requested; }
    bool sfxTermsAccepted() const;
    bool sfxTermsRequired() const;
    Q_INVOKABLE void acceptSfxTerms();
    Q_INVOKABLE QString modelTerms(bool english) const;
    bool ready(QString capability) const;
    bool ensure(QStringList capabilities);
    Q_INVOKABLE void requestDownload(QString group);
    Q_INVOKABLE void startDownload();
    Q_INVOKABLE void dismissRequest();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void verifyGroup(QString group);
    Q_INVOKABLE void removeGroup(QString group);
    Q_INVOKABLE void cleanPartials();
    Q_INVOKABLE void refresh() { emit changed(); }
    Q_INVOKABLE QString describeRequest() const;
    QVariantMap requestInfo() const;
    Q_INVOKABLE QString deletionDescription(QString group) const;
    Q_INVOKABLE void openRoot();
signals:
    void changed();
    void downloadRequested();
    void completed(bool success);
private:
    QStringList expand(QStringList capabilities) const;
    QList<ModelAsset> selected(QStringList groups) const;
    bool present(const ModelAsset &asset) const;
    bool safePath(QString relative) const;
    bool acquire();
    void finish(QString error={},bool cancelled=false);
    void next();
    void prepare(ModelAsset asset);
    void transfer(ModelAsset asset);
    void checkFile(ModelAsset asset,QString path,bool install);
    void advance(ModelAsset asset);
    void fail(ModelAsset asset,QString error);
    void updateProgress(QString path,qint64 bytes);
    QString m_root,m_endpoint,m_quant="Q4_K",m_status,m_error;
    QStringList m_requested;
    QList<ModelAsset> m_assets,m_queue;
    QVariantList m_groups;
    bool m_busy=false,m_cancelled=false,m_inference=false,m_verifyOnly=false;
    bool m_stopping=false;
    bool m_initialSetup=false,m_setupOffered=false;
    QSet<QString> m_active;
    QHash<QString,qint64> m_activeBytes;
    int m_index=0;
    qint64 m_total=0,m_done=0;
    double m_progress=0;
    QElapsedTimer m_refreshTimer;
    std::unique_ptr<QLockFile> m_lock;
    QUrl m_testTransport;
};
