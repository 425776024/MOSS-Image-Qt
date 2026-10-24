#pragma once
#include "ModelHttpDownload.h"
#include <QFutureWatcher>
#include <QJsonObject>
#include <QLockFile>
#include <QProcess>
#include <atomic>
#include <memory>

// Optional executable dependencies live outside the application/update directory.
// Each component is pinned by hash and is reusable across application versions.
class RuntimeManager : public QObject {
    Q_OBJECT
public:
    explicit RuntimeManager(QObject *parent=nullptr, QUrl testTransport={});
    ~RuntimeManager() override;
    static QString defaultRoot();
    static bool cudaAvailable();
    void prepare(QByteArray manifest, QString root=defaultRoot());
    void cancel();
    bool busy() const { return m_busy; }
signals:
    void progress(double value);
    void completed(QString directory, QString error, bool cancelled);
private:
    void next();
    void download();
    void extract();
    void assemble();
    void finish(QString error={},QString directory={});
    void background(std::function<QString()> work, std::function<void(QString)> done);
    QString componentPath() const;
    QString partPath() const;
    ModelHttpDownload m_download;
    QProcess m_extract;
    QTimer m_extractTimeout;
    QFutureWatcher<QString> m_check;
    std::unique_ptr<QLockFile> m_lock;
    std::shared_ptr<std::atomic_bool> m_cancelled;
    QJsonObject m_manifest,m_component;
    QUrl m_testTransport;
    QString m_root,m_staging;
    int m_index=0;
    qint64 m_total=0,m_done=0;
    bool m_busy=false;
};
