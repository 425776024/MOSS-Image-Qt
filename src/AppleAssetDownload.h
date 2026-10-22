#pragma once
#include <QObject>
#include <QString>
// Store-only native transport. ModelManager owns validation and atomic installation.
class AppleAssetDownload : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~AppleAssetDownload() override;
    void start(QString pack, QString relativePath, QString destination, QString noticeDestination, qint64 bytes);
    void cancel();
signals:
    void progress(double fraction);
    void completed(QString error);
private:
    void *m_job=nullptr;
};
