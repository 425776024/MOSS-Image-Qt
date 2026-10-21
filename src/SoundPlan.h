#pragma once
#include <QObject>
#include <QVariantMap>
#include <QVariantList>
#include <QJsonObject>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

class SoundPlan:public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(QVariantMap settings READ settings NOTIFY changed)
    Q_PROPERTY(QVariantList voices READ voices NOTIFY voicesChanged)
    Q_PROPERTY(bool canRestoreVoice READ canRestoreVoice NOTIFY voicesChanged)
public:
    explicit SoundPlan(QObject *parent=nullptr):QObject(parent){restore({});}
    QVariantMap settings()const{return m_settings;}
    QVariantList voices()const{return m_voices;}
    void initialize(QString outputRoot);
    void restore(QJsonObject json);
    QJsonObject json()const{return QJsonObject::fromVariantMap(m_settings);}
    QVariantMap selectedVoice()const;
    void setResult(QString key,QString value);
    Q_INVOKABLE void set(QString key,QVariant value);
    Q_INVOKABLE QString createVoice(QString name,QUrl audio,QString transcript);
    Q_INVOKABLE QString updateVoice(QString id,QString name,QString transcript);
    Q_INVOKABLE QString removeVoice(QString id);
    Q_INVOKABLE QString restoreVoice();
    bool canRestoreVoice() const;
signals:
    void changed();
    void edited();
    void settingEdited(QString key);
    void voicesChanged();
private:
    void loadVoices();
    QVariantMap m_settings;
    QVariantList m_voices;
    QString m_outputRoot;
};
