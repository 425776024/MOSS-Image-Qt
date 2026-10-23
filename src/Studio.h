#pragma once
#include "Document.h"
#include "ModelManager.h"
#include "EditDocument.h"
#include "LayerDocument.h"
#include "SoundPlan.h"
#include "TimelineDocument.h"
#include "RemoteImageClient.h"
#include "ConversationDocument.h"
#include <QObject>
#include <QProcess>
#include <QElapsedTimer>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QVariantList>

class Studio:public QObject {
    Q_OBJECT
    Q_PROPERTY(Document* document READ document CONSTANT)
    Q_PROPERTY(EditDocument* editDocument READ editDocument CONSTANT)
    Q_PROPERTY(LayerDocument* layerDocument READ layerDocument CONSTANT)
    Q_PROPERTY(SoundPlan* sound READ sound CONSTANT)
    Q_PROPERTY(TimelineDocument* timeline READ timeline CONSTANT)
    Q_PROPERTY(ModelManager* models READ models CONSTANT)
    Q_PROPERTY(QString modelRoot READ modelRoot NOTIFY changed)
    Q_PROPERTY(QString imageQuant READ imageQuant WRITE setImageQuant NOTIFY changed)
    Q_PROPERTY(QString imageDevice READ imageDevice WRITE setImageDevice NOTIFY changed)
    Q_PROPERTY(RemoteImageClient* remoteGpu READ remoteGpu CONSTANT)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString log READ log NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(double mouthCacheBytes READ mouthCacheBytes NOTIFY mouthCacheChanged)
    Q_PROPERTY(int mouthCacheLimitMiB READ mouthCacheLimitMiB CONSTANT)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    Q_PROPERTY(bool imageReady READ imageReady NOTIFY changed)
    Q_PROPERTY(bool dittoReady READ dittoReady NOTIFY changed)
    Q_PROPERTY(bool voiceReady READ voiceReady NOTIFY changed)
    Q_PROPERTY(QString avatarDesignImage READ avatarDesignImage NOTIFY changed)
    Q_PROPERTY(QVariantMap avatarDesignRecipe READ avatarDesignRecipe NOTIFY changed)
    Q_PROPERTY(QString voiceDesignAudio READ voiceDesignAudio NOTIFY changed)
    Q_PROPERTY(bool sfxReady READ sfxReady NOTIFY changed)
    Q_PROPERTY(QString imageUrl READ imageUrl NOTIFY changed)
    Q_PROPERTY(QString videoUrl READ videoUrl NOTIFY changed)
    Q_PROPERTY(QVariantList history READ history NOTIFY changed)
    Q_PROPERTY(QVariantList creationPages READ creationPages NOTIFY pagesChanged)
    Q_PROPERTY(ConversationDocument* conversation READ conversation NOTIFY conversationChanged)
    Q_PROPERTY(QVariantList layoutTemplates READ layoutTemplates NOTIFY templatesChanged)
    Q_PROPERTY(int currentPage READ currentPage NOTIFY pagesChanged)
    Q_PROPERTY(bool switchingPage READ switchingPage NOTIFY pageSwitchingChanged)
    Q_PROPERTY(QString creationImageUrl READ creationImageUrl NOTIFY changed)
    Q_PROPERTY(bool canRestorePage READ canRestorePage NOTIFY pagesChanged)
    Q_PROPERTY(bool repainting READ repainting WRITE setRepainting NOTIFY changed)
    Q_PROPERTY(bool narrating READ narrating WRITE setNarrating NOTIFY changed)
    Q_PROPERTY(QString pageAudio READ pageAudio WRITE setPageAudio NOTIFY changed)
    Q_PROPERTY(QString pageVisualUrl READ pageVisualUrl NOTIFY changed)
    Q_PROPERTY(QString canvasCompositeUrl READ canvasCompositeUrl NOTIFY changed)
    Q_PROPERTY(bool hasCanvasContent READ hasCanvasContent NOTIFY changed)
    Q_PROPERTY(QString pageVideoUrl READ pageVideoUrl NOTIFY changed)
    Q_PROPERTY(QString narrationRoute READ narrationRoute NOTIFY changed)
    Q_PROPERTY(QString portrait READ portrait WRITE setPortrait NOTIFY changed)
    Q_PROPERTY(QString audio READ audio WRITE setAudio NOTIFY changed)
    Q_PROPERTY(int steps MEMBER m_steps NOTIFY changed)
    Q_PROPERTY(int seed MEMBER m_seed NOTIFY changed)
    Q_PROPERTY(double cfg MEMBER m_cfg NOTIFY changed)
    Q_PROPERTY(double flowShift MEMBER m_flowShift NOTIFY changed)
    Q_PROPERTY(QString backend READ backend NOTIFY changed)
public:
    explicit Studio(QObject *parent=nullptr);
    ~Studio() override;
    Document* document(){return &m_doc;}
    EditDocument* editDocument(){return &m_edit;}
    LayerDocument* layerDocument(){return &m_layers;}
    SoundPlan* sound(){return &m_sound;}
    TimelineDocument* timeline(){return &m_timeline;}
    Q_INVOKABLE void generateTrack(QString key);
    Q_INVOKABLE void importTrackImage(QString key, QUrl url);
    Q_INVOKABLE void exportTrack(QString key, QUrl url);
    Q_INVOKABLE void exportTimeline(QUrl url);
    Q_INVOKABLE void exportCanvas(QUrl url);
    bool hasCanvasContent() const{return !m_pageVisual.isEmpty()||m_layers.hasContent();}
    QImage canvasImage() const;
    QString canvasCompositeUrl() const;
    ModelManager *models(){return &m_modelManager;}
    QString modelRoot()const{return m_models;}
    QString imageQuant()const{return m_imageQuant;}
    void setImageQuant(QString quant);
    QString imageDevice()const{return m_imageDevice;}
    void setImageDevice(QString device);
    RemoteImageClient* remoteGpu(){return &m_remote;}
    Q_INVOKABLE void resumeRemoteImage();
    QString status()const{return m_status;}
    QString error()const{return m_error;}
    QString log()const{return m_log;}
    bool busy()const{return m_busy||m_modelManager.busy();}
    double mouthCacheBytes()const{return m_mouthCacheBytes;}
    int mouthCacheLimitMiB()const{return 128;}
    Q_INVOKABLE void refreshMouthCache();
    Q_INVOKABLE QString clearMouthCache();
    double progress()const{return m_progress;}
    bool imageReady()const;
    bool dittoReady()const;
    bool voiceReady()const;
    bool sfxReady()const;
    QString avatarDesignImage()const{return m_avatarDesignImage;}
    QVariantMap avatarDesignRecipe()const{return m_avatarDesignRecipe;}
    Q_INVOKABLE void generateAvatarImage(QString description);
    QString voiceDesignAudio()const{return m_voiceDesignAudio;}
    Q_INVOKABLE void generateVoiceDesign(QString description,QString text);
    Q_INVOKABLE QString saveDesignedVoice(QString name);
    QString imageUrl()const{return m_image;}
    QString videoUrl()const{return m_video;}
    QVariantList history()const{return m_history;}
    QVariantList creationPages()const{return m_pages;}
    ConversationDocument *conversation() const { return m_conversation; }
    Q_INVOKABLE ConversationDocument *conversationFor(QString id);
    Q_INVOKABLE void openConversation(QString id);
    Q_INVOKABLE void newConversation();
    Q_INVOKABLE void sendConversation();
    Q_INVOKABLE void exportConversationAsset(QString id, QUrl destination);
    Q_INVOKABLE void saveConversation(QUrl destination);
    Q_INVOKABLE bool deleteConversationMessage(QString workId, QString messageId, bool cleanup);
    Q_INVOKABLE bool prepareConversationEdit();
    Q_INVOKABLE bool prepareConversationSettings();
    QVariantList layoutTemplates() const;
    Q_INVOKABLE QString importLayoutTemplate(QUrl source);
    Q_INVOKABLE void removeLayoutTemplate(QString id);
    Q_INVOKABLE void useLayoutTemplate(QString id);
    int currentPage()const{return m_currentPage;}
    bool switchingPage()const{return m_loadingPage;}
    QString creationImageUrl()const{return m_pageImage;}
    bool canRestorePage()const{return !m_deletedPage.isEmpty();}
    bool repainting()const{return m_repainting;}
    void setRepainting(bool enabled);
    bool narrating()const{return m_narrating;}
    void setNarrating(bool enabled);
    QString pageAudio()const{return m_pageAudio;}
    void setPageAudio(QString path);
    QString pageVisualUrl()const{return m_pageVisual;}
    QString pageVideoUrl()const{return m_pageVideo;}
    QString narrationRoute()const{return m_narrationRoute;}
    Q_INVOKABLE void usePageVisual(bool edited);
    Q_INVOKABLE void generatePageVideo(double limitSeconds=0);
    Q_INVOKABLE void generateWithSound(bool repaint=false);
    Q_INVOKABLE void exportPageVideo(QUrl url);
    Q_INVOKABLE void newPage();
    Q_INVOKABLE void renamePage(int index, QString name);
    Q_INVOKABLE void flushWorkspace(){persist();}
    Q_INVOKABLE void selectPage(int index);
    Q_INVOKABLE void duplicatePage(int index);
    Q_INVOKABLE void deletePage(int index);
    Q_INVOKABLE void restoreDeletedPage();
    Q_INVOKABLE void exportCreationResult(QUrl url);
    Q_INVOKABLE void selectCurrentCreationImage(){m_image=m_pageImage;emit changed();}
    QString portrait()const{return m_portrait;}
    QString audio()const{return m_audio;}
    QString backend()const{return m_backend;}
    void setPortrait(QString s);
    void setAudio(QString s);
    Q_INVOKABLE void generate();
    Q_INVOKABLE void generateEdit();
    Q_INVOKABLE void importEditImage(QUrl url);
    Q_INVOKABLE void editGeneratedImage();
    Q_INVOKABLE void useEditResult();
    Q_INVOKABLE bool editComposition();
    Q_INVOKABLE void addGeneratedLayer();
    Q_INVOKABLE void generateAvatar(double limitSeconds=0);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void saveProject(QUrl url);
    void loadProject(QUrl url);
    Q_INVOKABLE void exportResult(QUrl url, bool video=false);
    Q_INVOKABLE void exportEditResult(QUrl url);
    Q_INVOKABLE void selectHistory(int index);
    Q_INVOKABLE void useGeneratedPortrait();
    Q_INVOKABLE void setModelRoot(QUrl url);
    Q_INVOKABLE void downloadModels();
    Q_INVOKABLE void openOutputs();
    Q_INVOKABLE void dismissError(){m_error.clear();emit changed();}
    Q_INVOKABLE void copyText(QString text);
    Q_INVOKABLE QString readText(QUrl url);
    Q_INVOKABLE void saveText(QUrl url, QString text);
    Q_INVOKABLE void randomizeSeed();
    QString outputRoot()const{return m_outputRoot;}
signals:
    void fileExportFinished(bool success, QString detail);
    void conversationSaveFinished(bool success, QString detail);
    void mouthCacheChanged();
    void changed();
    void jobFinished(bool success);
    void pagesChanged();
    void pageSwitchingChanged();
    void pageActivated();
    void conversationChanged();
    void templatesChanged();
private:
    double m_mouthCacheBytes=0;
    QString engine(QString name)const;
    void fail(QString text);
    void start(QString exe,QStringList args,QString kind);
    void readOutput();
    void consumeOutput(QByteArray bytes);
    void startRemote(QStringList args,QString kind);
    void finished(int code,QProcess::ExitStatus exitStatus);
    void refreshHistory();
    bool ensureModels(QStringList capabilities);
    void persist();
    void exportFile(QString source,QUrl destination);
    QJsonObject recipe() const;
    void restoreRecipe(const QJsonObject &recipe);
    void generateImage(const Document &document,QString kind,int steps);
    void advanceAudioPipeline();
    bool validateSound();
    void renderPageVideo();
    QString effectiveSpeech() const;
    void loadPages();
    void syncTimelineSources();
    void soundSettingEdited(QString key);
    QString canvasSnapshot();
    void capturePage();
    bool savePages();
    bool activatePage(int index);
    void finishConversation(bool success);
    void packageConversation();
    QVariantMap makePage(const QJsonObject &document, const QJsonObject &generation, QString image = {}) const;
    Document m_doc;
    EditDocument m_edit;
    LayerDocument m_layers;
    SoundPlan m_sound;
    TimelineDocument m_timeline;
    QString m_trackGeneration;
    QString m_avatarDesignImage;
    QVariantMap m_avatarDesignRecipe;
    QString m_voiceDesignAudio,m_voiceDesignText,m_voiceDesignDescription;
    QUrl m_timelineExport;
    QProcess m_process;
    RemoteImageClient m_remote;
    QElapsedTimer m_timer;
    ModelManager m_modelManager;
    QFile m_jobLog;
    QString m_models,m_outputRoot,m_status="准备创作",m_error,m_log,m_image,m_video,m_portrait,m_audio,m_backend="待运行";
    QString m_imageQuant="Q4_K";
    QString m_imageDevice="local";
    bool m_remoteJob=false;
    QString m_jobDir,m_kind,m_pending;
    QVariantList m_history;
    QVariantList m_pages;
    QHash<QString,ConversationDocument*> m_conversations;
    ConversationDocument *m_conversation=nullptr,*m_conversationJob=nullptr;
    QString m_conversationTask;
    QVariantMap m_conversationTarget;
    QVariantList m_conversationClips;
    QString m_editConversationTarget;
    QVariantMap m_deletedPage;
    int m_currentPage=-1,m_deletedIndex=0;
    QString m_pageImage;
    QString m_canvasSnapshot;
    QString m_canvasSnapshotBase;
    quint64 m_canvasRevision=0,m_canvasSnapshotRevision=0;
    bool m_loadingPage=false;
    bool m_repainting=false;
    bool m_narrating=false;
    QString m_pageAudio,m_pageVideo,m_pageVisual,m_narrationRoute,m_workerError;
    bool m_audioPipeline=false,m_voiceDone=false,m_sfxDone=false;
    double m_pageVideoLimit=0;
    bool m_busy=false,m_cancelled=false;
    double m_progress=0;
    int m_steps=50,m_seed=42;
    double m_cfg=3,m_flowShift=5;
};
