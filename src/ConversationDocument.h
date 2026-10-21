#pragma once
#include <QObject>
#include <QJsonObject>
#include <QVariantList>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

// One durable conversation per work. UI selection never restores the inference
// documents, so browsing another work cannot retarget an in-flight job.
class ConversationDocument : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(QString id READ id CONSTANT)
    Q_PROPERTY(QVariantList messages READ messages NOTIFY messagesChanged)
    Q_PROPERTY(QVariantList assets READ assets NOTIFY assetsChanged)
    Q_PROPERTY(QString task READ task WRITE setTask NOTIFY changed)
    Q_PROPERTY(QString draft READ draft WRITE setDraft NOTIFY changed)
    Q_PROPERTY(QVariantMap target READ target NOTIFY changed)
    Q_PROPERTY(QVariantMap audio READ audio NOTIFY changed)
    Q_PROPERTY(QString voiceId READ voiceId WRITE setVoiceId NOTIFY changed)
    Q_PROPERTY(int sfxDuration READ sfxDuration WRITE setSfxDuration NOTIFY changed)
    Q_PROPERTY(QVariantList selected READ selected NOTIFY clipsChanged)
    Q_PROPERTY(QVariantList clips READ clips NOTIFY clipsChanged)
    Q_PROPERTY(double duration READ duration NOTIFY clipsChanged)
    Q_PROPERTY(bool manual READ manual NOTIFY clipsChanged)
    Q_PROPERTY(bool pending READ pending NOTIFY messagesChanged)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    ConversationDocument(QString id, QString root, const QJsonObject &json, QObject *parent=nullptr);
    QString id() const { return m_id; }
    QVariantList messages() const { return m_messages; }
    QVariantList assets() const { return m_assets; }
    QString task() const { return m_task; }
    QString draft() const { return m_drafts.value(m_task).toString(); }
    QVariantMap target() const;
    QVariantMap audio() const { return asset(m_audioId); }
    QString voiceId() const { return m_voiceId; }
    int sfxDuration() const { return m_sfxDuration; }
    QVariantList selected() const { return m_selected; }
    QVariantList clips() const { return m_clips; }
    double duration() const;
    bool manual() const { return m_manual; }
    bool pending() const { return !m_pending.isEmpty(); }
    QString error() const { return m_error; }
    void setTask(QString task);
    void setDraft(QString text);
    void setVoiceId(QString id);
    void setSfxDuration(int seconds);
    Q_INVOKABLE QVariantMap asset(QString id) const;
    Q_INVOKABLE void chooseTarget(QString id);
    Q_INVOKABLE void chooseAudio(QString id);
    Q_INVOKABLE QString importMedia(QUrl url, QString purpose="");
    Q_INVOKABLE void toggleSelected(QString id);
    Q_INVOKABLE void moveClip(QString id, double start);
    Q_INVOKABLE void arrange();
    Q_INVOKABLE void dismissError();
    QString addAsset(QString path, QString kind, QString name={}, QString poster={}, QString audioId={});
    void addResults(const QStringList &ids, QString task, QString text={});
    bool beginRequest();
    bool removeMessage(QString id, QVariantList *orphaned=nullptr);
    void markTemporary(QString id, QString directory);
    void finishRequest(bool success, const QStringList &ids, QString error);
    QJsonObject json() const;
    void report(QString message);
signals:
    void changed();
    void messagesChanged();
    void assetsChanged();
    void clipsChanged();
    void edited();
private:
    void rebuildClips();
    void probe(QString id);
    void touch();
    QString m_id,m_root,m_task="image",m_audioId,m_voiceId,m_pending,m_pendingTask,m_error;
    QVariantMap m_drafts,m_targets;
    QVariantList m_messages,m_assets,m_selected,m_clips;
    int m_sfxDuration=5;
    bool m_manual=false;
};
