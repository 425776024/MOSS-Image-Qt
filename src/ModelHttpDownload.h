#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QFile>
#include <QTimer>
#include <QElapsedTimer>
#include <QVector>

// One pinned file, with bounded probes and parallel ranges. Installation and SHA-256
// verification remain the caller's responsibility. Never cache signed redirect URLs.
class ModelHttpDownload : public QObject {
    Q_OBJECT
public:
    struct Policy {
        int connections=4, probeMs=4000, idleMs=15000, firstByteMs=60000, slowMs=5000, cooldownMs=30000;
        qint64 probeBytes=2*1024*1024, parallelThreshold=8*1024*1024, slowBytesPerSecond=1000000;
    };
    explicit ModelHttpDownload(QObject *parent=nullptr);
    ~ModelHttpDownload() override;
    void setPolicy(Policy policy) { m_policy=policy; }
    void start(QString partPath,qint64 bytes,QString identity,QList<QUrl> urls,QString cacheKey,bool automatic);
    void cancel();
    static qint64 savedBytes(const QString &path,qint64 total,const QString &identity);
    static QString journalPath(const QString &path) { return path+".ranges.json"; }
    static void clearSourceCache();
signals:
    void progress(qint64 bytes);
    void completed(QString error,bool cancelled);
    void sourceSelected(QUrl url);
private:
    struct Range { qint64 begin=0,end=0,received=0; QPointer<QNetworkReply> reply; QElapsedTimer activity; bool hasData=false; };
    struct Probe { QPointer<QNetworkReply> reply; QElapsedTimer clock; qint64 bytes=0; double speed=0; bool valid=false; };
    bool restore();
    bool checkpoint();
    qint64 received() const;
    void probe();
    void switchSlowSource(double speed);
    void finishProbes();
    void beginTransfer(int source);
    void launch(int index);
    void consume(int index,QNetworkReply *reply);
    void retry(QString error);
    void stopReplies();
    void fallbackToStream();
    void finish(QString error={},bool cancelled=false);
    QNetworkAccessManager m_network;
    QFile m_file;
    QList<QUrl> m_urls;
    QVector<Range> m_ranges;
    QVector<Probe> m_probes;
    Policy m_policy;
    QString m_path,m_identity,m_cacheKey;
    qint64 m_total=0,m_windowBytes=0;
    QTimer m_probeTimer,m_watchdog;
    QElapsedTimer m_window,m_checkpoint,m_cooldown;
    QVector<int> m_failures;
    QVector<double> m_speeds;
    QVector<bool> m_slowTried;
    int m_source=0,m_generation=0;
    bool m_active=false,m_auto=false,m_probing=false,m_stream=false,m_rangeConfirmed=false;
};
