#include "Studio.h"
#include "UserSelectedFileExport.h"
#include "ConversationMediaStore.h"
#include "../vendor/xinshen-ditto/liveportrait/mouth_cache_store.h"
#include <QCoreApplication>
#include <QStandardPaths>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QSaveFile>
#include <QSettings>
#include <QDateTime>
#include <QUuid>
#include <QDesktopServices>
#include <QRegularExpression>
#include <QImageReader>
#include <QTimer>
#include <QCryptographicHash>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <cmath>
#include <QGuiApplication>
#include <QClipboard>
#include <QRandomGenerator>

namespace {
struct ModelFile {QString name,sha;};
QList<ModelFile> modelFiles(const QString &quant){
    const QStringList names={"MOSS-Image-SFT-"+quant+".gguf","Qwen3.5-9B-Q4_K_M.gguf","flux2-vae_small.safetensors","tokenizer.json"};
    QList<ModelFile> result;for(const auto &name:names)for(const auto &asset:ModelManager::assets())if(asset.path==name)result.append({name,asset.sha256});return result;
}
bool writeJson(QString path,QJsonObject data){
    QSaveFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(QJsonDocument(data).toJson())>=0&&f.commit();
}
QJsonObject readJson(QString path){QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};return QJsonDocument::fromJson(f.readAll()).object();}
QString local(QString s){return s.startsWith("file:")?QUrl(s).toLocalFile():s;}
QString ownedWorkerInput(QString s){
    const auto source=local(s);
#ifdef MOSS_APP_STORE
    // PowerBox access belongs to the UI process; give workers container-owned bytes.
    const auto dir=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/imports";
    if(!QDir().mkpath(dir))return {};
    const auto destination=dir+"/"+QUuid::createUuid().toString(QUuid::WithoutBraces)+"-"+QFileInfo(source).fileName();
    return QFile::copy(source,destination)?destination:QString();
#else
    return source;
#endif
}
QString digest(QString path){
    QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};
    QCryptographicHash h(QCryptographicHash::Sha256);if(!h.addData(&f))return {};return h.result().toHex();
}
}
Studio::Studio(QObject *p):QObject(p),m_doc(this),m_edit(this),m_layers(this),m_sound(this),m_timeline(this) {
    QSettings settings;
    m_outputRoot=qEnvironmentVariable("MOSS_OUTPUT_ROOT",QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/works");
#ifdef MOSS_APP_STORE
    m_outputRoot=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/works";
#endif
    m_models=m_modelManager.root();m_imageQuant=m_modelManager.imageQuant();
    connect(&m_modelManager,&ModelManager::changed,this,[this]{
        m_models=m_modelManager.root();m_imageQuant=m_modelManager.imageQuant();
        if(!m_busy){m_status=m_modelManager.status();m_progress=m_modelManager.progress();m_error=m_modelManager.error();}
        emit changed();
    });
    auto previousModelError=std::make_shared<QString>();
    connect(&m_modelManager,&ModelManager::changed,this,[this,previousModelError]{
        const auto error=m_modelManager.error();
        if(!error.isEmpty()&&error!=*previousModelError)
            DiagnosticLog::instance().record("error","model-error",{{"message",error}});
        *previousModelError=error;
    });
    connect(this,&Studio::changed,this,[this]{m_modelManager.setInferenceBusy(m_busy);});
    if(m_remote.available()&&qEnvironmentVariable("MOSS_IMAGE_DEVICE",settings.value("imageDevice","local").toString())=="remote")m_imageDevice="remote";
    connect(&m_remote,&RemoteImageClient::changed,this,&Studio::changed);
    connect(&m_remote,&RemoteImageClient::archivesChanged,this,&Studio::refreshHistory);
    connect(&m_remote,&RemoteImageClient::output,this,&Studio::consumeOutput);
    connect(&m_remote,&RemoteImageClient::phase,this,[this](QString text){if(m_remoteJob){m_status=text;emit changed();}});
    connect(&m_remote,&RemoteImageClient::completed,this,[this](int code,bool cancelled,bool recoverable){
        m_cancelled=cancelled;m_workerError=m_remote.error();
        if(recoverable)m_workerError+="\n任务记录已保留，连接后可在远程 GPU 设置中恢复回收。";
        finished(code,QProcess::NormalExit);
    });
    QDir().mkpath(m_outputRoot);
    m_sound.initialize(m_outputRoot);
    m_process.setProcessChannelMode(QProcess::MergedChannels);
    connect(&m_process,&QProcess::readyReadStandardOutput,this,&Studio::readOutput);
    connect(&m_process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,&Studio::finished);
    connect(&m_process,&QProcess::errorOccurred,this,[this](QProcess::ProcessError e){
        m_diagnostics.processError(int(e),m_process.errorString());
        if(e==QProcess::FailedToStart){
            m_jobLog.close();m_diagnostics.finish(false,false,-1,false,m_process.errorString());
            m_busy=false;m_audioPipeline=false;fail("无法启动本地引擎："+m_process.errorString());emit jobFinished(false);
        }
    });
    auto diagnosticHeartbeat=new QTimer(this);diagnosticHeartbeat->setInterval(10000);
    connect(diagnosticHeartbeat,&QTimer::timeout,this,[this]{m_diagnostics.heartbeat();});
    diagnosticHeartbeat->start();
    auto session=readJson(m_outputRoot+"/session.json");
    if(!session.isEmpty()){
        QString error;m_doc.load(session["document"].toObject(),&error);
        restoreRecipe(session);
        m_portrait=session["portrait"].toString();m_audio=session["audio"].toString();
    }
    auto autosave=new QTimer(this);autosave->setInterval(800);autosave->setSingleShot(true);
    connect(&m_doc,&Document::changed,autosave,qOverload<>(&QTimer::start));
    connect(&m_edit,&EditDocument::changed,autosave,[this,autosave]{if(!m_loadingPage)autosave->start();});
    connect(&m_sound,&SoundPlan::changed,autosave,[this,autosave]{if(!m_loadingPage)autosave->start();});
    connect(&m_sound,&SoundPlan::settingEdited,this,&Studio::soundSettingEdited);
    connect(&m_timeline,&TimelineDocument::changed,autosave,[this,autosave]{if(!m_loadingPage)autosave->start();});
    connect(&m_layers,&LayerDocument::changed,this,[this,autosave]{
        if(!m_loadingPage){autosave->start();emit changed();}
    });
    connect(&m_layers,&LayerDocument::contentChanged,this,[this]{
        ++m_canvasRevision;
        if(m_loadingPage)return;
        m_pageVideo.clear();m_narrationRoute.clear();syncTimelineSources();
        emit changed();
    });
    connect(this,&Studio::changed,autosave,[this,autosave]{if(!m_loadingPage)autosave->start();});
    connect(autosave,&QTimer::timeout,this,&Studio::persist);
    refreshHistory();
    loadPages();
    connect(this,&Studio::jobFinished,this,&Studio::finishConversation);
    refreshMouthCache();
    connect(this,&Studio::jobFinished,this,[this](bool){refreshMouthCache();});
    if(m_imageDevice=="remote")QTimer::singleShot(0,&m_remote,[this]{m_remote.connectServer();});
}
Studio::~Studio(){persist();if(m_process.state()!=QProcess::NotRunning){m_process.kill();m_process.waitForFinished(3000);}}
QUrl Studio::defaultDiagnosticsExportUrl() const{
    return QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)+
        "/Xinshen-diagnostics-"+QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss")+".log");
}
void Studio::exportDiagnostics(QUrl destination){
    QString error;
    if(!destination.isLocalFile()||destination.toLocalFile().isEmpty()){
        emit diagnosticsExportFinished(false,"请选择本地文件");return;
    }
    const bool success=DiagnosticLog::instance().exportTo(destination.toLocalFile(),&error);
    emit diagnosticsExportFinished(success,success?destination.toLocalFile():error);
}
void Studio::refreshMouthCache(){
    try {
        const auto path=(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)+"/ditto-mouth-v2").toStdString();
        ditto::mouth_cache::Lock lock(path);
        m_mouthCacheBytes=ditto::mouth_cache::prune(path);
        emit mouthCacheChanged();
    } catch(const std::exception &) { /* Retry when Settings opens or the job completes. */ }
}
QString Studio::clearMouthCache(){
    if(m_busy)return "生成期间无法清理缓存，请稍后重试";
    try {
        const auto path=(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)+"/ditto-mouth-v2").toStdString();
        ditto::mouth_cache::Lock lock(path);
        m_mouthCacheBytes=ditto::mouth_cache::prune(path,ditto::mouth_cache::limitBytes,true);
        emit mouthCacheChanged();
        return "缓存已清理，下次生成将重新校准";
    } catch(const std::exception &) {return "缓存正在使用或无法清理，请稍后重试";}
}
QJsonObject Studio::recipe()const{return {{"steps",m_steps},{"seed",m_seed},{"cfg",m_cfg},{"flowShift",m_flowShift}};}
void Studio::restoreRecipe(const QJsonObject &r){
    m_steps=std::clamp(r["steps"].toInt(50),1,100);m_seed=std::max(0,r["seed"].toInt(42));
    m_cfg=std::clamp(r["cfg"].toDouble(3),1.,10.);m_flowShift=std::clamp(r["flowShift"].toDouble(5),1.,10.);
}
void Studio::persist(){
    if(m_loadingPage)return;
    auto s=recipe();s["document"]=m_doc.json();s["portrait"]=m_portrait;s["audio"]=m_audio;
    writeJson(m_outputRoot+"/session.json",s);
    capturePage();savePages();
}
QVariantMap Studio::makePage(const QJsonObject &document,const QJsonObject &generation,QString image)const{
    return {{"id",QUuid::createUuid().toString(QUuid::WithoutBraces)},
        {"document",document.toVariantMap()},{"generation",generation.toVariantMap()},
        {"image",image},{"created",QDateTime::currentDateTime().toString(Qt::ISODate)},
        {"modified",QDateTime::currentDateTime().toString(Qt::ISODate)}};
}
void Studio::capturePage(){
    if(m_loadingPage||m_currentPage<0||m_currentPage>=m_pages.size())return;
    auto page=m_pages[m_currentPage].toMap();
    page["document"]=m_doc.json().toVariantMap();page["generation"]=recipe().toVariantMap();
    page["image"]=m_pageImage;page["edit"]=m_edit.snapshot().toVariantMap();page["repainting"]=m_repainting;
    page["narrating"]=m_narrating;page["audio"]=m_pageAudio;page["video"]=m_pageVideo;
    page["visual"]=m_pageVisual;page["narrationRoute"]=m_narrationRoute;
    page["sound"]=m_sound.json().toVariantMap();page["timeline"]=m_timeline.snapshot().toVariantMap();
    page["canvasLayers"]=m_layers.json().toVariantMap();
    if(auto thread=m_conversations.value(page["id"].toString()))page["conversation"]=thread->json().toVariantMap();
    if(page!=m_pages[m_currentPage].toMap()){
        page["modified"]=QDateTime::currentDateTime().toString(Qt::ISODate);
        m_pages[m_currentPage]=page;emit pagesChanged();
    }
}
bool Studio::savePages(){
    for(int i=0;i<m_pages.size();++i){auto row=m_pages[i].toMap();if(auto thread=m_conversations.value(row["id"].toString())){row["conversation"]=thread->json().toVariantMap();m_pages[i]=row;}}
    if(writeJson(m_outputRoot+"/creation-pages.json",{{"version",5},{"current",m_currentPage},{"activeConversation",m_conversation?m_conversation->id():QString()},
        {"pages",QJsonArray::fromVariantList(m_pages)}}))return true;
    // Notify once per error so the autosave observer cannot form a retry loop.
    const QString message="无法保存创作页面，请检查作品目录的空间和权限";
    if(m_error!=message){m_error=message;emit changed();}
    return false;
}
bool Studio::activatePage(int index){
    if(index<0||index>=m_pages.size())return false;
    const auto page=m_pages[index].toMap();QString error;
    const auto canvasLayers=QJsonObject::fromVariantMap(page["canvasLayers"].toMap());
    if(!canvasLayers.isEmpty()){
        LayerDocument validator;
        if(!validator.load(canvasLayers)){fail(validator.message());return false;}
    }
    m_loadingPage=true;
    emit pageSwitchingChanged();
    const bool valid=m_doc.switchPage(QJsonObject::fromVariantMap(page["document"].toMap()),&error);
    if(valid){
        m_currentPage=index;restoreRecipe(QJsonObject::fromVariantMap(page["generation"].toMap()));
        m_layers.restoreSnapshot(canvasLayers);m_canvasSnapshot.clear();
        m_pageImage=page["image"].toString();m_image=m_pageImage;
        m_edit.restoreSnapshot(QJsonObject::fromVariantMap(page["edit"].toMap()));
        m_repainting=page["repainting"].toBool();
        m_narrating=page["narrating"].toBool();if(m_narrating)m_repainting=false;
        m_pageAudio=page["audio"].toString();m_pageVideo=page["video"].toString();
        auto sound=QJsonObject::fromVariantMap(page["sound"].toMap());
        if(sound.isEmpty()&&!m_pageAudio.isEmpty())sound["voiceSource"]="import";
        m_sound.restore(sound);
        m_pageVisual=page["visual"].toString();m_narrationRoute=page["narrationRoute"].toString();
        if(m_pageVisual.isEmpty())m_pageVisual=!m_edit.resultUrl().isEmpty()?m_edit.resultUrl():
            !m_pageImage.isEmpty()?m_pageImage:m_edit.ready()?QUrl::fromLocalFile(m_edit.sourcePath()).toString():QString();
        m_timeline.restore(QJsonObject::fromVariantMap(page["timeline"].toMap()));
        if(page["timeline"].toMap()["tracks"].toList().isEmpty())syncTimelineSources();
    }
    m_loadingPage=false;
    emit pageSwitchingChanged();
    if(!valid){fail(error);return false;}
    openConversation(page["id"].toString());
    emit pagesChanged();emit changed();emit pageActivated();return true;
}
void Studio::loadPages(){
    const auto stored=readJson(m_outputRoot+"/creation-pages.json");
    const auto pages=stored["pages"].toArray();
    for(const auto &value:pages){
        const auto page=value.toObject();Document validator;QString error;
        if(validator.load(page["document"].toObject(),&error))
            m_pages.append(page.toVariantMap());
    }
    if(m_pages.isEmpty()){
        // Migrate completed image works without changing their files. Preserve
        // the current session as a draft even when no image has been generated.
        for(auto it=m_history.crbegin();it!=m_history.crend();++it){
            const auto job=it->toMap();if(job["kind"]!="image")continue;
            const auto project=readJson(job["directory"].toString()+"/project.json");
            Document validator;QString error;if(!validator.load(project,&error))continue;
            auto page=makePage(project,project.contains("generation")?project["generation"].toObject():QJsonObject::fromVariantMap(job),job["url"].toString());
            page["sourceJob"]=job["directory"];m_pages.append(page);
        }
        m_pages.append(makePage(m_doc.json(),recipe()));
    }
    const int current=pages.isEmpty()?int(m_pages.size()-1):std::clamp(stored["current"].toInt(),0,int(m_pages.size()-1));
    // Earlier builds saved one global composition. Import it once into the
    // previously active page and retain the original file for compatibility.
    if(stored["version"].toInt()<4&&!m_pages[current].toMap().contains("canvasLayers")){
        const auto legacy=readJson(m_outputRoot+"/layers-session.mosslayers.json");
        LayerDocument validator;
        if(!legacy.isEmpty()&&validator.load(legacy)){
            auto page=m_pages[current].toMap();page["canvasLayers"]=legacy.toVariantMap();m_pages[current]=page;
        }
    }
    activatePage(current);
    if(!stored["activeConversation"].toString().isEmpty())openConversation(stored["activeConversation"].toString());
    savePages();
}
void Studio::selectPage(int index){
    if(busy()||index<0||index>=m_pages.size()||index==m_currentPage)return;
    capturePage();if(!savePages()){emit changed();return;}
    if(activatePage(index)){m_status="已切换创作页";savePages();}
}
void Studio::newPage(){
    if(busy())return;capturePage();if(!savePages()){emit changed();return;}
    Document blank;
    m_pages.append(makePage(blank.json(),recipe()));
    activatePage(m_pages.size()-1);m_status="已新建创作页";savePages();
}
void Studio::renamePage(int index,QString name){
    if(busy()||index<0||index>=m_pages.size()||name.trimmed().isEmpty())return;
    capturePage();auto page=m_pages[index].toMap();page["name"]=name.trimmed().left(120);
    page["modified"]=QDateTime::currentDateTime().toString(Qt::ISODate);m_pages[index]=page;
    savePages();emit pagesChanged();
}
void Studio::duplicatePage(int index){
    if(busy()||index<0||index>=m_pages.size())return;
    capturePage();if(!savePages()){emit changed();return;}
    auto page=m_pages[index].toMap();page["id"]=QUuid::createUuid().toString(QUuid::WithoutBraces);
    if(page.contains("name"))page["name"]=page["name"].toString()+" 副本";
    page.remove("sourceJob");page.remove("editSourceJob");page.remove("videoSourceJob");
    page["created"]=QDateTime::currentDateTime().toString(Qt::ISODate);
    m_pages.insert(index+1,page);activatePage(index+1);m_status="已复制创作页";savePages();
}
void Studio::deletePage(int index){
    if(busy()||index<0||index>=m_pages.size())return;
    capturePage();if(!savePages()){emit changed();return;}
    const auto viewedId=m_conversation?m_conversation->id():QString();
    m_deletedPage=m_pages[index].toMap();m_deletedIndex=index;
    const bool deletingViewed=m_deletedPage["id"].toString()==viewedId;
    const bool deletingActive=index==m_currentPage;
    m_pages.removeAt(index);
    if(m_pages.isEmpty()){Document blank;m_pages.append(makePage(blank.json(),recipe()));}
    const int next=deletingActive?std::min(index,int(m_pages.size()-1)):m_currentPage-(index<m_currentPage?1:0);
    activatePage(next);
    if(!deletingViewed)openConversation(viewedId);
    else openConversation(m_pages[std::min(index,int(m_pages.size()-1))].toMap()["id"].toString());
    m_status="作品已移除，可恢复；生成文件仍保留";savePages();
}
void Studio::restoreDeletedPage(){
    if(busy()||m_deletedPage.isEmpty())return;
    capturePage();if(!savePages()){emit changed();return;}
    const int index=std::min(m_deletedIndex,int(m_pages.size()));
    m_pages.insert(index,m_deletedPage);m_deletedPage.clear();activatePage(index);savePages();
}
void Studio::exportCreationResult(QUrl url){exportFile(local(m_pageImage),url);}
void Studio::copyText(QString text){QGuiApplication::clipboard()->setText(text);}
void Studio::randomizeSeed(){if(busy())return;m_seed=int(QRandomGenerator::global()->bounded(2147483647u));emit changed();}
QString Studio::readText(QUrl url){
    QFile f(url.toLocalFile());
    if(!f.open(QIODevice::ReadOnly)||f.size()>2*1024*1024){fail("无法读取文件，或文件大于 2 MB");return {};}
    return QString::fromUtf8(f.readAll());
}
void Studio::saveText(QUrl url,QString text){
    QSaveFile f(url.toLocalFile());const auto bytes=text.toUtf8();
    if(!f.open(QIODevice::WriteOnly)||f.write(bytes)!=bytes.size()||!f.commit())fail("保存文件失败");
    else{m_status="输入内容已导出";emit changed();}
}
QString Studio::engine(QString name)const{
    QString executable=name;
#ifdef Q_OS_WIN
    executable+=".exe";
#endif
    QStringList paths={QCoreApplication::applicationDirPath()+"/"+executable,
        QCoreApplication::applicationDirPath()+"/../Helpers/"+executable,
        };
    if(!qEnvironmentVariableIsSet("MOSS_DISABLE_DEV_PATHS"))
        paths.append(QCoreApplication::applicationDirPath()+"/../../../workers/"+executable);
    if(name=="sa3-generate"&&!qEnvironmentVariableIsSet("MOSS_DISABLE_DEV_PATHS"))
        paths.append(QString(MOSS_SOURCE_ROOT)+"/.local/build-audio/bin/"+executable);
    if(!qEnvironmentVariableIsSet("MOSS_DISABLE_DEV_PATHS"))
        paths.append(QString(MOSS_SOURCE_ROOT)+(name=="sd-cli"?"/.local/build-engine/bin/":"/build/workers/")+executable);
    for(auto p:paths)if(QFileInfo(p).isExecutable())return QDir::cleanPath(p);
    return {};
}
bool Studio::imageReady()const{
    if(m_imageDevice=="remote")return m_remote.modelReady(m_imageQuant);
    if(engine("sd-cli").isEmpty())return false;
    return m_modelManager.ready("image");
}
bool Studio::ensureModels(QStringList capabilities){
    if(m_modelManager.busy())return false;
    if(!m_modelManager.ensure(capabilities)){m_audioPipeline=false;return false;}
    for(const auto &id:capabilities){
        const auto worker=id=="image"?"sd-cli":id=="ditto"?"moss-ditto":id=="voice"?"moss-voice":"sa3-generate";
        if(engine(worker).isEmpty()){fail("模型已就绪，但未找到推理引擎："+QString(worker));return false;}
    }
    return true;
}
void Studio::setImageDevice(QString device){
    if((device=="remote"&&!m_remote.available())||busy()||m_remote.busy()||(device!="local"&&device!="remote")||device==m_imageDevice)return;
    m_imageDevice=device;QSettings().setValue("imageDevice",device);m_error.clear();
    m_status=device=="remote"?"已选择远程 GPU":"已选择本机";emit changed();
}
void Studio::setImageQuant(QString quant){if(!busy())m_modelManager.setImageQuant(quant);}
bool Studio::dittoReady()const{return !engine("moss-ditto").isEmpty()&&m_modelManager.ready("ditto");}
bool Studio::voiceReady()const{return !engine("moss-voice").isEmpty()&&m_modelManager.ready("voice");}
bool Studio::sfxReady()const{return !engine("sa3-generate").isEmpty()&&m_modelManager.ready("sfx");}
void Studio::setPortrait(QString s){m_portrait=ownedWorkerInput(s);if(m_portrait.isEmpty())m_error="无法导入人像，请重新选择文件";emit changed();}
void Studio::setAudio(QString s){m_audio=ownedWorkerInput(s);if(m_audio.isEmpty())m_error="无法导入音频，请重新选择文件";emit changed();}
void Studio::fail(QString s){DiagnosticLog::instance().record("error","operation-error",{{"message",s}});m_error=s;m_status="操作未完成";if(!m_busy)m_audioPipeline=false;emit changed();}
void Studio::start(QString exe,QStringList args,QString kind){
    if(m_conversationJob)ConversationMediaStore::mark(m_jobDir);
    m_remoteJob=false;
    m_kind=kind;m_cancelled=false;m_busy=true;m_progress=0;m_backend="正在检测";m_error.clear();m_workerError.clear();m_log.clear();m_pending.clear();
    m_status=kind=="voice"?"正在合成页面配音…":kind=="sfx"?"正在生成音效…":kind=="page-video"?"正在检测画面并准备配音…":kind=="timeline"?"正在导出时间轴…":kind=="avatar"?"正在准备 Ditto…":"正在加载 MOSS-Image…";
    auto diagnosticParameters=readJson(m_jobDir+"/job.json");
    diagnosticParameters["imageQuant"]=m_imageQuant;
    diagnosticParameters["offloadToCpu"]=args.contains("--offload-to-cpu");
    diagnosticParameters["vaeTiling"]=args.contains("--vae-tiling");
    m_diagnostics.begin(kind,QFileInfo(exe).fileName(),false,diagnosticParameters);
    m_jobLog.setFileName(m_jobDir+"/engine.log");
    if(!m_jobLog.open(QIODevice::WriteOnly)){
        m_diagnostics.finish(false,false,-1,false,m_jobLog.errorString());
        m_busy=false;fail("无法写入任务日志，请检查作品目录的空间和权限");emit jobFinished(false);return;
    }
    auto env=QProcessEnvironment::systemEnvironment();
    // Use the known default Ditto profile, independently of experimental shells.
    for(auto k:env.keys())if(k.startsWith("XINSHEN_")||k.startsWith("MOSS_FLOWEDIT_"))env.remove(k);
    env.insert("XINSHEN_COREML_CACHE",QStandardPaths::writableLocation(QStandardPaths::CacheLocation)+"/coreml");
    env.insert("XINSHEN_DITTO_MOUTH_CACHE_DIR",QStandardPaths::writableLocation(QStandardPaths::CacheLocation)+"/ditto-mouth-v2");
    const auto editJob=kind=="edit"?readJson(m_jobDir+"/job.json"):QJsonObject();
    if(editJob["editMethod"]=="flowedit"){
        env.insert("MOSS_FLOWEDIT_MODE","flow");
        env.insert("MOSS_FLOWEDIT_SOURCE_PROMPT",m_jobDir+"/source-prompt.txt");
        env.insert("MOSS_FLOWEDIT_NMAX",QString::number(editJob["flowSteps"].toInt()));
        env.insert("MOSS_FLOWEDIT_NMIN","0");env.insert("MOSS_FLOWEDIT_NAVG","1");
        env.insert("MOSS_FLOWEDIT_SOURCE_CFG",QString::number(editJob["cfg"].toDouble()));
    }
    m_process.setProcessEnvironment(env);m_timer.start();persist();m_process.start(exe,args);emit changed();
}
void Studio::startRemote(QStringList args,QString kind){
    if(m_conversationJob)ConversationMediaStore::mark(m_jobDir);
    m_remoteJob=true;m_kind=kind;m_busy=true;m_cancelled=false;m_progress=0;m_backend="正在检测";
    m_error.clear();m_workerError.clear();m_log.clear();m_pending.clear();m_timer.start();
    m_diagnostics.begin(kind,"sd-cli",true,readJson(m_jobDir+"/job.json"));
    m_jobLog.setFileName(m_jobDir+"/remote-stream.log");
    if(!m_jobLog.open(QIODevice::WriteOnly)){
        m_diagnostics.finish(false,false,-1,false,m_jobLog.errorString());
        m_busy=false;m_remoteJob=false;fail("无法保存远程任务日志");emit jobFinished(false);return;
    }
    persist();
    if(!m_remote.start(m_jobDir,kind,m_imageQuant,args)){
        m_workerError=m_remote.error().isEmpty()?"请先连接远程 GPU 并确认模型已就绪":m_remote.error();
        finished(1,QProcess::NormalExit);
    }
    emit changed();
}
void Studio::resumeRemoteImage(){
    if(!m_remote.available()||busy()||m_remote.busy()||!m_remote.ready())return;
    const auto directory=m_remote.latestPending();if(directory.isEmpty())return;
    const auto job=readJson(directory+"/job.json"),project=readJson(directory+"/project.json");
    const auto pending=readJson(directory+"/remote-pending.json");
    setImageQuant(pending["quant"].toString());
    if(!project.isEmpty())loadProject(QUrl::fromLocalFile(directory+"/project.json"));
    else{
        newPage();
        restoreRecipe(job);
        if(job["kind"]=="edit"){
            const auto error=m_edit.load(directory+"/source.png");
            if(!error.isEmpty()){fail(error);return;}
            m_edit.restoreMask(directory+(QFileInfo::exists(directory+"/local-selection.png")?"/local-selection.png":"/selection.png"));m_edit.m_prompt=job["prompt"].toString();
            m_edit.m_strength=job["strength"].toDouble();m_edit.m_feather=job["feather"].toInt();
            m_edit.m_blendMode=job["blendMode"].toString()=="direct"?"direct":"feather";
            m_edit.m_scope=job["scope"].toString()=="whole"?"whole":"local";
            m_edit.m_editMethod=job["editMethod"].toString()=="flowedit"?"flowedit":"standard";
            m_edit.m_sourcePrompt=job["sourcePrompt"].toString();
            m_edit.m_flowStrength=job["flowStrength"].toDouble(20.0/28.0);
            m_repainting=true;
        }
    }
    m_imageDevice="remote";m_jobDir=directory;m_kind=job["kind"].toString();
    m_remoteJob=true;m_busy=true;m_cancelled=false;m_progress=0;m_backend="正在检测";
    m_error.clear();m_workerError.clear();m_log.clear();m_pending.clear();m_timer.start();
    m_diagnostics.begin(m_kind,"sd-cli-resumed",true,job);
    m_jobLog.setFileName(m_jobDir+"/remote-stream.log");
    if(!m_jobLog.open(QIODevice::WriteOnly|QIODevice::Append)||!m_remote.resume(directory)){
        m_workerError="无法恢复远程任务";finished(1,QProcess::NormalExit);return;
    }
    emit changed();
}
void Studio::generate(){generateImage(m_doc,"image",m_steps);}
void Studio::generateAvatarImage(QString description){
    if(busy())return;
    m_avatarDesignImage.clear();m_avatarDesignRecipe.clear();emit changed();
    description=description.trimmed();
    if(description.isEmpty()||description.size()>4000){fail("请填写人物形象描述（最多四千字）");return;}
    m_audioPipeline=false;m_trackGeneration.clear();
    Document portrait;portrait.setPrompt(description);portrait.setCanvasSize(768,1024);
    generateImage(portrait,"avatar-image",16);
}
void Studio::generateImage(const Document &document,QString kind,int steps){
    if(busy())return;
    auto error=document.validationError();if(!error.isEmpty()){fail(error);return;}
    if(m_imageDevice=="local"&&!ensureModels({"image"}))return;
    if(m_imageDevice=="remote"&&!imageReady()){fail("请先在模型设置中连接远程 GPU");return;}
    if(steps<1||steps>100||!std::isfinite(m_cfg)||m_cfg<1||m_cfg>10||
       !std::isfinite(m_flowShift)||m_flowShift<1||m_flowShift>10||m_seed<0){fail("采样参数超出有效范围");return;}
    m_jobDir=(m_imageDevice=="remote"?m_remote.archiveRoot():m_outputRoot)+"/"+QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss")+"-"+QUuid::createUuid().toString(QUuid::Id128).left(6);
    QDir().mkpath(m_jobDir);
    QFile prompt(m_jobDir+"/prompt.txt");if(!prompt.open(QIODevice::WriteOnly)){fail("无法创建任务文件");return;}prompt.write(document.compiledPrompt().toUtf8());prompt.close();
    auto project=document.json();project["generation"]=recipe();auto generation=project["generation"].toObject();generation["steps"]=steps;project["generation"]=generation;writeJson(m_jobDir+"/project.json",project);
    const auto files=modelFiles(m_imageQuant);
    writeJson(m_jobDir+"/job.json",{{"kind",kind},{"steps",steps},{"seed",m_seed},{"cfg",m_cfg},{"flowShift",m_flowShift},{"negativePrompt",document.negativePrompt()},{"model","MOSS-Image-SFT-"+m_imageQuant},{"modelSha256",files[0].sha},{"textEncoder","Qwen3.5-9B-Q4_K_M"},{"started",QDateTime::currentDateTime().toString(Qt::ISODate)}});
    if(kind=="avatar-image")m_avatarDesignRecipe={{"engine","MOSS-Image"},{"prompt",document.prompt()},{"model","MOSS-Image-SFT-"+m_imageQuant},{"modelSha256",files[0].sha},{"steps",steps},{"seed",m_seed},{"width",document.canvasWidth()},{"height",document.canvasHeight()}};
    QStringList args={"--diffusion-model",m_models+"/"+files[0].name,"--llm",m_models+"/"+files[1].name,
        "--vae",m_models+"/"+files[2].name,"--tokenizer",m_models+"/"+files[3].name,
        "--prompt-file",m_jobDir+"/prompt.txt","-W",QString::number(document.canvasWidth()),"-H",QString::number(document.canvasHeight()),
        "--steps",QString::number(steps),"--cfg-scale",QString::number(m_cfg),"--seed",QString::number(m_seed),
        "--sampling-method","euler","--scheduler","discrete","--flow-shift",QString::number(m_flowShift),
        "--offload-to-cpu","--vae-tiling","--rng","cpu","--output",m_jobDir+"/image.png","-v"};
    if(!document.negativePrompt().isEmpty())args<< "--negative-prompt" <<document.negativePrompt();
    auto job=readJson(m_jobDir+"/job.json");job["executionDevice"]=m_imageDevice;writeJson(m_jobDir+"/job.json",job);
    if(m_imageDevice=="remote")startRemote(args,kind);else start(engine("sd-cli"),args,kind);
}
void Studio::generateAvatar(double limit){
    if(busy())return;
    if(!ensureModels({"ditto"}))return;
    if(!QFileInfo::exists(m_portrait)||!QFileInfo::exists(m_audio)){fail("请选择一张人像和一段驱动音频");return;}
    if(!QImageReader(m_portrait).canRead()){fail("请选择可读取的人像图片");return;}
    m_jobDir=m_outputRoot+"/"+QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss")+"-ditto-"+QUuid::createUuid().toString(QUuid::Id128).left(6);
    QDir().mkpath(m_jobDir);
    writeJson(m_jobDir+"/request.json",{{"models",m_models+"/ditto-original"},{"image",m_portrait},{"audio",m_audio},{"directory",m_jobDir},{"seed",m_seed},{"mouthReference",m_sound.settings()["mouthReference"].toString()},{"limitSeconds",limit}});
    writeJson(m_jobDir+"/job.json",{{"kind","avatar"},{"seed",m_seed},{"started",QDateTime::currentDateTime().toString(Qt::ISODate)}});
    start(engine("moss-ditto"),{"generate",m_jobDir+"/request.json"},"avatar");
}
void Studio::importEditImage(QUrl url){
    if(busy())return;
    EditDocument imported;const auto error=imported.load(url.toLocalFile());
    if(!error.isEmpty()){fail(error);return;}
    // Keep an immutable local source so deleting/moving the imported file cannot
    // break an existing page. Copies share this source, but own their masks.
    const auto directory=m_outputRoot+"/creation-assets";
    const auto source=directory+"/"+QUuid::createUuid().toString(QUuid::WithoutBraces)+".png";
    QSaveFile file(source);
    if(!QDir().mkpath(directory)||!file.open(QIODevice::WriteOnly)||
       !imported.source().save(&file,"PNG")||!file.commit()){fail("无法保存重绘原图");return;}
    m_edit.load(source);m_repainting=true;m_narrating=false;
    m_pageVisual=QUrl::fromLocalFile(source).toString();m_pageVideo.clear();m_narrationRoute.clear();syncTimelineSources();
    m_error.clear();m_status="已导入原图，请涂抹或画框选择修改区域";emit changed();
}
void Studio::setRepainting(bool enabled){
    if(busy()||(enabled==m_repainting&&!m_narrating))return;
    if(enabled&&!m_edit.ready()&&!m_pageImage.isEmpty()){
        importEditImage(QUrl(m_pageImage));
        if(!m_edit.ready())return;
    }
    m_repainting=enabled;m_narrating=false;emit changed();
}
void Studio::setNarrating(bool enabled){
    if(busy())return;m_narrating=enabled;
    if(enabled)m_repainting=false;
    emit changed();
}
void Studio::setPageAudio(QString path){
    if(busy())return;path=local(path);
    if(path==m_pageAudio){m_sound.set("voiceSource",path.isEmpty()?"none":"import");return;}
    if(!path.isEmpty()){
        if(!QFileInfo(path).isFile()){fail("请选择可读取的配音音频");return;}
        const auto directory=m_outputRoot+"/creation-assets/"+QUuid::createUuid().toString(QUuid::WithoutBraces);
        const auto stored=directory+"/"+QFileInfo(path).fileName();
        if(!QDir().mkpath(directory)||!QFile::copy(path,stored)){fail("无法保存配音音频");return;}
        path=stored;
    }
    m_pageAudio=path;m_pageVideo.clear();m_narrationRoute.clear();
    m_sound.set("voiceSource",path.isEmpty()?"none":"import");syncTimelineSources();emit changed();
}
void Studio::usePageVisual(bool edited){
    if(busy())return;
    const auto source=edited?m_edit.resultUrl():m_pageImage;
    if(source.isEmpty()||source==m_pageVisual)return;
    m_pageVisual=source;m_pageVideo.clear();m_narrationRoute.clear();syncTimelineSources();emit changed();
}
QString Studio::effectiveSpeech()const{
    const auto settings=m_sound.settings();const auto source=settings["voiceSource"].toString();
    return source=="import"?m_pageAudio:source=="tts"?settings["speechResult"].toString():QString();
}
void Studio::generateWithSound(bool repaint){
    if(busy())return;
    m_trackGeneration="all-tracks";
    const auto s=m_sound.settings();
    m_audioPipeline=s["voiceSource"]!="none"||s["sfxEnabled"].toBool();
    if(m_audioPipeline&&!validateSound())return;
    m_voiceDone=false;m_sfxDone=false;m_pageVideoLimit=0;
    if(repaint)generateEdit();else generate();
}
void Studio::generatePageVideo(double limit){
    if(busy())return;
    m_trackGeneration.clear();
    if(!validateSound())return;
    m_audioPipeline=true;m_voiceDone=false;m_sfxDone=false;m_pageVideoLimit=limit;
    if(!hasCanvasContent()){generate();return;}
    advanceAudioPipeline();
}
bool Studio::validateSound(){
    const auto settings=m_sound.settings();const auto source=settings["voiceSource"].toString();
    QString error;
    if(m_trackGeneration!="sfx"&&source=="tts"){
        if(settings["speechText"].toString().trimmed().isEmpty())error="请输入需要合成的配音文案";
        else if(m_sound.selectedVoice().isEmpty())error="请选择一个内置或自定义音色";
        else if(!QFileInfo::exists(settings["speechResult"].toString())&&!ensureModels({"voice"}))return false;
    }else if(m_trackGeneration!="sfx"&&source=="import"&&!QFileInfo::exists(m_pageAudio))error="请选择本页要使用的配音音频";
    if(error.isEmpty()&&m_trackGeneration!="speech"&&m_trackGeneration!="voice-only"&&settings["sfxEnabled"].toBool()){
        if(settings["sfxPrompt"].toString().trimmed().isEmpty())error="请输入音效描述";
        else if(!QFileInfo::exists(settings["sfxResult"].toString())&&!ensureModels({"sfx"}))return false;
    }
    if(error.isEmpty()&&source=="none"&&!settings["sfxEnabled"].toBool())error="请添加配音或启用音效";
    if(!error.isEmpty()){fail(error);return false;}
    return true;
}
void Studio::generateVoiceDesign(QString description,QString text){
    if(busy())return;
    m_voiceDesignAudio.clear();emit changed();
    description=description.trimmed();text=text.trimmed();
    if(description.isEmpty()||description.size()>1000||text.isEmpty()||text.size()>120){fail("请填写声音描述（最多一千字）和试听文案（最多一百二十字）");return;}
    if(!ensureModels({"voice"}))return;
    m_voiceDesignAudio.clear();m_voiceDesignText=text;m_voiceDesignDescription=description;
    m_audioPipeline=false;m_trackGeneration.clear();
    m_jobDir=m_outputRoot+"/"+QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss")+"-voice-design-"+QUuid::createUuid().toString(QUuid::Id128).left(6);
    if(!QDir().mkpath(m_jobDir)||!writeJson(m_jobDir+"/request.json",{{"mode","design"},{"models",m_models+"/voice"},{"description",description},{"text",text},{"language","zh"},{"output",m_jobDir+"/voice.wav"}})){
        fail("无法保存语音设计任务");return;
    }
    writeJson(m_jobDir+"/job.json",{{"kind","voice-design"},{"engine","ming-3.0-q8"},{"description",description},{"text",text}});
    start(engine("moss-voice"),{m_jobDir+"/request.json"},"voice-design");
}
QString Studio::saveDesignedVoice(QString name){
    if(busy())return "请等待生成结束";
    if(m_voiceDesignAudio.isEmpty())return "请先生成并试听设计音色";
    return m_sound.createVoice(name,QUrl(m_voiceDesignAudio),m_voiceDesignText,
        {{"engine","ming-3.0-q8"},{"description",m_voiceDesignDescription},{"text",m_voiceDesignText},{"seed",1895}});
}
void Studio::advanceAudioPipeline(){
    if(!m_audioPipeline||m_busy)return;
    const auto settings=m_sound.settings();
    const auto source=settings["voiceSource"].toString();
    const auto newJob=[this](QString suffix){
        m_jobDir=m_outputRoot+"/"+QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss")+"-"+suffix+"-"+QUuid::createUuid().toString(QUuid::Id128).left(6);
        return QDir().mkpath(m_jobDir);
    };
    if(source=="tts"&&!m_voiceDone){
        m_voiceDone=true;
        if(settings["speechText"].toString().trimmed().isEmpty()){fail("请输入需要合成的配音文案");return;}
        const auto voice=m_sound.selectedVoice();
        if(voice.isEmpty()){fail("请选择一个内置或自定义音色");return;}
        if(!QFileInfo::exists(settings["speechResult"].toString())){
            const auto worker=engine("moss-voice");
            if(!ensureModels({"voice"}))return;
            if(!newJob("voice")){fail("无法创建语音任务目录");return;}
            if(!writeJson(m_jobDir+"/request.json",{{"models",m_models+"/voice"},{"reference",voice["reference"].toString()},
                {"referenceText",voice["transcript"].toString()},{"emotion",settings["emotion"].toString()},{"dialect",settings["dialect"].toString()},{"language",voice["language"].toString().section('-',0,0).isEmpty()?"zh":voice["language"].toString().section('-',0,0)},{"text",settings["speechText"].toString()},{"output",m_jobDir+"/voice.wav"}})){
                fail("无法保存语音任务");return;
            }
            writeJson(m_jobDir+"/job.json",{{"kind","voice"},{"engine","ming-3.0-q8"},{"modelRevision","2eb8d37c7f10672e69c8aa33091baa04c461b66c"},{"emotion",settings["emotion"].toString()},{"dialect",settings["dialect"].toString()},{"voiceId",settings["voiceId"].toString()},{"text",settings["speechText"].toString()}});
            start(worker,{m_jobDir+"/request.json"},"voice");return;
        }
    }
    if(m_trackGeneration!="sfx"&&source=="import"&&!QFileInfo::exists(m_pageAudio)){fail("请选择本页要使用的配音音频");return;}
    if(m_trackGeneration!="speech"&&m_trackGeneration!="voice-only"&&settings["sfxEnabled"].toBool()&&!m_sfxDone){
        m_sfxDone=true;
        if(settings["sfxPrompt"].toString().trimmed().isEmpty()){fail("请输入音效描述");return;}
        if(!QFileInfo::exists(settings["sfxResult"].toString())){
            const auto worker=engine("sa3-generate");const auto models=m_models+"/sound-effects";
            if(!ensureModels({"sfx"}))return;
            if(!newJob("sfx")){fail("无法创建音效任务目录");return;}
            writeJson(m_jobDir+"/job.json",{{"kind","sfx"},{"engine","stable-audio-3-medium-f16"},{"prompt",settings["sfxPrompt"].toString()},{"duration",settings["sfxDuration"].toInt()}});
            start(worker,{"--model","medium","--models-dir",models,"--prompt",settings["sfxPrompt"].toString(),
                "--duration",QString::number(settings["sfxDuration"].toInt()),"--steps","40","--seed",QString::number(m_seed),
                "--out",m_jobDir+"/sfx.wav"},"sfx");return;
        }
    }
    if(m_trackGeneration=="sfx"||m_trackGeneration=="voice-only"||(m_trackGeneration=="speech"&&!hasCanvasContent())||(m_trackGeneration=="all-tracks"&&effectiveSpeech().isEmpty())){
        syncTimelineSources();m_audioPipeline=false;m_status=m_trackGeneration=="sfx"?"音效已更新到轨道":m_trackGeneration=="all-tracks"?"轨道内容已生成":"配音已更新到轨道";persist();emit changed();return;
    }
    if(effectiveSpeech().isEmpty()&&!settings["sfxEnabled"].toBool()){fail("请添加配音或启用音效");return;}
    m_narrating=true;m_repainting=false;renderPageVideo();
}
void Studio::renderPageVideo(){
    if(busy())return;
    const auto speech=effectiveSpeech();const auto settings=m_sound.settings();
    const auto effect=m_trackGeneration.isEmpty()&&settings["sfxEnabled"].toBool()?settings["sfxResult"].toString():QString();
    const auto audio=speech.isEmpty()?effect:speech;
    if(!hasCanvasContent()||!QFileInfo::exists(audio)){
        fail("请先为本页准备图像并选择配音音频");return;
    }
    if(!speech.isEmpty()&&!ensureModels({"ditto"}))return;
    const auto canvas=canvasSnapshot();if(canvas.isEmpty())return;
    const auto worker=engine("moss-ditto");if(worker.isEmpty()){fail("未找到本地视频引擎");return;}
    m_jobDir=m_outputRoot+"/"+QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss")+"-page-"+QUuid::createUuid().toString(QUuid::Id128).left(6);
    if(!QDir().mkpath(m_jobDir)){fail("无法创建视频输出目录");return;}
    if(!writeJson(m_jobDir+"/request.json",{{"models",m_models+"/ditto-original"},{"image",canvas},
        {"audio",audio},{"speech",!speech.isEmpty()},{"sfx",speech.isEmpty()?QString():effect},{"sfxGain",settings["sfxGain"].toDouble()},
        {"directory",m_jobDir},{"seed",m_seed},{"mouthReference",settings["mouthReference"].toString()},{"limitSeconds",m_pageVideoLimit}})||
       !writeJson(m_jobDir+"/job.json",{{"kind","page-video"},{"seed",m_seed},{"visual",QUrl::fromLocalFile(canvas).toString()},
        {"audio",audio},{"started",QDateTime::currentDateTime().toString(Qt::ISODate)}})){
        fail("无法保存配音视频任务");return;
    }
    m_narrationRoute.clear();start(worker,{"generate-page",m_jobDir+"/request.json"},"page-video");
    // Store the editable page next to the exported work for history recovery.
    capturePage();writeJson(m_jobDir+"/page.json",QJsonObject::fromVariantMap(m_pages[m_currentPage].toMap()));
}
void Studio::exportPageVideo(QUrl url){exportFile(local(m_pageVideo),url);}
void Studio::editGeneratedImage(){if(!m_pageImage.isEmpty())importEditImage(QUrl(m_pageImage));}
void Studio::useEditResult(){if(!m_edit.resultUrl().isEmpty())importEditImage(QUrl(m_edit.resultUrl()));}
void Studio::addGeneratedLayer(){
    if(!m_busy&&!m_image.isEmpty())m_layers.importFiles({QUrl(m_image)});
}
bool Studio::editComposition(){
    if(busy()||!m_layers.hasContent())return false;
    const auto directory=m_outputRoot+"/composites/"+QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss")+"-"+QUuid::createUuid().toString(QUuid::Id128).left(6);
    if(!QDir().mkpath(directory)){fail("无法创建合成快照");return false;}
    if(!m_layers.exportPng(QUrl::fromLocalFile(directory+"/source.png"))||
       !m_layers.saveProject(QUrl::fromLocalFile(directory+"/composition.mosslayers.json"))){fail(m_layers.message());return false;}
    EditDocument validator;const auto error=validator.load(directory+"/source.png");
    if(!error.isEmpty()){fail(error);return false;}
    const auto count=m_pages.size();newPage();if(m_pages.size()==count)return false;
    m_edit.load(directory+"/source.png");m_repainting=true;m_narrating=false;
    m_pageVisual=QUrl::fromLocalFile(directory+"/source.png").toString();syncTimelineSources();
    m_error.clear();m_status="已合成到局部重绘，原图层与素材已保留";emit changed();return true;
}
void Studio::generateEdit(){
    if(busy())return;
    if(!m_edit.ready()){fail("请先选择需要编辑的图片");return;}
    if(m_edit.m_scope!="whole"&&!m_edit.hasMask()){fail("请先涂抹或框选需要修改的区域");return;}
    if(m_edit.m_prompt.trimmed().isEmpty()){fail("请描述修改后的画面");return;}
    const bool flow=m_edit.usesFlowEdit();
    if(flow&&m_edit.m_sourcePrompt.trimmed().isEmpty()){fail("请在图生图中填写原图描述");return;}
    if(flow&&m_edit.m_sourcePrompt.toUtf8().size()>65536){fail("请将原图描述缩短后重试");return;}
    if(flow&&m_imageDevice!="local"){fail("请切换到本机使用保留原貌");return;}
    if(m_imageDevice=="local"&&!ensureModels({"image"}))return;
    if(m_imageDevice=="remote"&&!imageReady()){fail("请先在模型设置中连接远程 GPU");return;}
    if(m_steps<1||m_steps>100||!std::isfinite(m_cfg)||m_cfg<1||m_cfg>10||
       (flow&&(!std::isfinite(m_edit.m_flowStrength)||m_edit.m_flowStrength<.1||m_edit.m_flowStrength>1))||
       !std::isfinite(m_edit.m_strength)||m_edit.m_strength<.1||m_edit.m_strength>1){fail("重绘参数超出有效范围");return;}
    if(flow){
        QProcess capability;capability.start(engine("sd-cli"),{"--moss-flowedit-version"});
        if(!capability.waitForFinished(3000)||capability.exitStatus()!=QProcess::NormalExit||capability.exitCode()!=0||
           capability.readAllStandardOutput().trimmed()!="MOSS_FLOWEDIT_V1"){
            capability.kill();capability.waitForFinished(1000);fail("请更新图像引擎后使用保留原貌");return;
        }
    }
    const int flowSteps=flow?std::clamp(qRound(m_steps*m_edit.m_flowStrength),1,m_steps):0;
    m_jobDir=(m_imageDevice=="remote"?m_remote.archiveRoot():m_outputRoot)+"/"+QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss")+"-edit-"+QUuid::createUuid().toString(QUuid::Id128).left(6);
    QDir().mkpath(m_jobDir);
    QString error;if(!m_edit.prepare(m_jobDir,&error)){fail(error);return;}
    QFile prompt(m_jobDir+"/prompt.txt");
    if(!prompt.open(QIODevice::WriteOnly)){fail("无法创建任务提示词");return;}
    prompt.write(m_edit.m_prompt.toUtf8());prompt.close();
    if(flow){
        QFile original(m_jobDir+"/source-prompt.txt");const auto bytes=m_edit.m_sourcePrompt.toUtf8();
        if(!original.open(QIODevice::WriteOnly)||original.write(bytes)!=bytes.size()){fail("无法保存原图描述");return;}
    }
    const int w=std::max(256,(m_edit.imageWidth()+15)/16*16),h=std::max(256,(m_edit.imageHeight()+15)/16*16);
    const auto files=modelFiles(m_imageQuant);
    const auto blendMode=m_edit.m_scope=="whole"?QString("direct"):m_edit.m_blendMode;
    writeJson(m_jobDir+"/job.json",{{"kind","edit"},{"steps",m_steps},{"seed",m_seed},{"cfg",m_cfg},
        {"editMethod",flow?"flowedit":"standard"},{"sourcePrompt",m_edit.m_sourcePrompt},
        {"flowStrength",m_edit.m_flowStrength},{"flowSteps",flowSteps},
        {"strength",m_edit.m_strength},{"feather",m_edit.m_feather},{"blendMode",blendMode},{"scope",m_edit.m_scope},{"prompt",m_edit.m_prompt},
        {"sourcePath",m_edit.sourcePath()},{"width",m_edit.imageWidth()},{"height",m_edit.imageHeight()},
        {"inferenceWidth",w},{"inferenceHeight",h},{"model","MOSS-Image-SFT-"+m_imageQuant},
        {"modelSha256",files[0].sha},{"textEncoder","Qwen3.5-9B-Q4_K_M"},
        {"outsideMaskPolicy",blendMode=="direct"?"model-output":"exact-source-pixels"},{"started",QDateTime::currentDateTime().toString(Qt::ISODate)}});
    QStringList args={"--diffusion-model",m_models+"/"+files[0].name,"--llm",m_models+"/"+files[1].name,
        "--vae",m_models+"/"+files[2].name,"--tokenizer",m_models+"/"+files[3].name,
        "--init-img",m_jobDir+"/init.png","--strength",QString::number(flow?1.0:m_edit.m_strength),
        "--prompt-file",m_jobDir+"/prompt.txt","-W",QString::number(w),"-H",QString::number(h),
        "--steps",QString::number(m_steps),"--cfg-scale",QString::number(m_cfg),"--seed",QString::number(m_seed),
        "--sampling-method","euler","--scheduler","discrete","--flow-shift","5","--offload-to-cpu",
        "--vae-tiling","--rng","cpu","--output",m_jobDir+"/raw.png","-v"};
    if(!flow)args<<"--mask"<<m_jobDir+"/mask.png";
    auto job=readJson(m_jobDir+"/job.json");job["executionDevice"]=m_imageDevice;writeJson(m_jobDir+"/job.json",job);
    if(m_imageDevice=="remote")startRemote(args,"edit");else{
        start(engine("sd-cli"),args,"edit");if(busy())m_status="正在准备图像编辑…";emit changed();
    }
}
void Studio::readOutput(){
    consumeOutput(m_process.readAllStandardOutput());
}
void Studio::consumeOutput(QByteArray bytes){
    if(bytes.isEmpty())return;
    m_diagnostics.consume(bytes);
    if(m_jobLog.isOpen()){m_jobLog.write(bytes);m_jobLog.flush();}
    const auto text=QString::fromUtf8(bytes);m_log=(m_log+text).right(18000);m_pending+=text;
    QStringList backendNames;
    const auto observed=m_diagnostics.summary()["components"].toObject();
    for(const auto &component:observed){
        const auto name=component.toObject()["backend"].toString();
        if(!name.isEmpty()&&!backendNames.contains(name))backendNames.append(name);
    }
    if(!backendNames.isEmpty())m_backend=backendNames.join(" / ")+(m_remoteJob?" · 远程":"");
    if(m_log.contains("generating image"))m_status="正在生成图像…";
    if(m_log.contains("decoding"))m_status="正在解码图像…";
    const QRegularExpression step(m_kind=="sfx"?R"(step\s+(\d+)\s*/\s*(\d+))":R"((\d+)\s*/\s*(\d+)\s*[-|])");
    auto matches=step.globalMatch(text);while(matches.hasNext()){
        auto match=matches.next();int n=match.captured(1).toInt(),total=match.captured(2).toInt();
        if(total>0){m_progress=double(n)/total;m_status=QString("正在生成 · %1 / %2").arg(n).arg(total);}
    }
    int end;while((end=m_pending.indexOf('\n'))>=0){
        auto line=m_pending.left(end);m_pending.remove(0,end+1);
        auto j=QJsonDocument::fromJson(line.toUtf8()).object();
        if(j["event"]=="error")m_workerError=j["message"].toString();
        if(j["event"]=="voice-backend")m_backend=(j["backend"]=="vulkan"?"Vulkan":"CPU")+QString(" · Ming-3.0 Q8");
        if(j["event"]=="page-route"){
            m_narrationRoute=j["route"].toString();
            m_status=m_narrationRoute=="portrait"?"检测到人像，正在生成说话视频…":"未检测到人像，正在合成画面配音…";
        }
        if(j["event"]=="progress"){
            QString stage=j["stage"].toString();auto total=j["total"].toDouble();
            if(total>0)m_progress=j["done"].toDouble()/total;
            m_status=stage=="timeline-video"?"正在导出时间轴视频…":stage.startsWith("voice")?(m_kind=="voice-design"?"正在设计语音…":"正在合成配音文案…"):stage=="page-video"?"正在合成图像配音视频…":stage=="render"?"正在生成数字人画面…":stage=="ditto-mouth-calibration"?"正在校准人物嘴形（首次使用）…":stage=="ditto-audio"?"正在提取语音特征…":stage=="ditto-motion"?"正在生成表情动作…":"正在加载数字人模型…";
        }
    }
    if(m_pending.size()>20000)m_pending=m_pending.right(20000);
    emit changed();
}
void Studio::finished(int code,QProcess::ExitStatus state){
    if(!m_remoteJob)readOutput();m_jobLog.close();m_busy=false;
    bool success=!m_cancelled&&code==0&&state==QProcess::NormalExit;
    QString output;
    if(m_kind=="voice-design"){
        output=m_jobDir+"/voice.wav";success=success&&QFileInfo(output).size()>44;
        if(success)m_voiceDesignAudio=QUrl::fromLocalFile(output).toString();
    }else if(m_kind=="voice"||m_kind=="sfx"){
        output=m_jobDir+(m_kind=="voice"?"/voice.wav":"/sfx.wav");
        success=success&&QFileInfo(output).size()>44;
        if(success){m_sound.setResult(m_kind=="voice"?"speechResult":"sfxResult",output);syncTimelineSources();}
    }else if(m_kind=="edit"){
        const QImage raw(m_jobDir+"/raw.png");
        output=m_jobDir+"/image.png";
        if(success){
            const auto job=readJson(m_jobDir+"/job.json");
            const auto composed=EditDocument::finishImage(QImage(m_jobDir+"/source.png"),raw,QImage(m_jobDir+"/blend.png"),job["blendMode"].toString());
            success=!composed.isNull()&&composed.save(output);
        }
        if(success){
            m_image=QUrl::fromLocalFile(output).toString();m_edit.setResult(output);
            m_pageVisual=m_image;m_pageVideo.clear();m_narrationRoute.clear();syncTimelineSources();
            capturePage();
            if(m_currentPage>=0){auto page=m_pages[m_currentPage].toMap();page["editSourceJob"]=m_jobDir;m_pages[m_currentPage]=page;}
            savePages();emit pagesChanged();
        }
    }else if(m_kind=="avatar-image"){
        output=m_jobDir+"/image.png";if(!QFileInfo::exists(output))output=m_jobDir+"/image_1.png";
        success=success&&QImageReader(output).canRead();
        if(success)m_avatarDesignImage=QUrl::fromLocalFile(output).toString();
    }else if(m_kind=="image"){
        output=m_jobDir+"/image.png";
        if(!QFileInfo::exists(output))output=m_jobDir+"/image_1.png";
        success=success&&QImageReader(output).canRead();
        if(success){
            m_image=QUrl::fromLocalFile(output).toString();m_pageImage=m_image;
            m_pageVisual=m_image;m_pageVideo.clear();m_narrationRoute.clear();syncTimelineSources();
            capturePage();
            if(m_currentPage>=0){auto page=m_pages[m_currentPage].toMap();page["sourceJob"]=m_jobDir;m_pages[m_currentPage]=page;}
            savePages();emit pagesChanged();
        }
    }else{
        output=readJson(m_jobDir+"/result.json")["path"].toString();
        success=success&&QFileInfo(output).size()>0;
        if(success)m_video=QUrl::fromLocalFile(output).toString();
        if(success&&m_kind=="page-video"){
            m_pageVideo=m_video;m_narrationRoute=readJson(m_jobDir+"/result.json")["route"].toString();syncTimelineSources();
            capturePage();
            if(m_currentPage>=0){auto page=m_pages[m_currentPage].toMap();page["videoSourceJob"]=m_jobDir;m_pages[m_currentPage]=page;
                writeJson(m_jobDir+"/page.json",QJsonObject::fromVariantMap(page));}
            savePages();emit pagesChanged();
        }
    }
    auto job=readJson(m_jobDir+"/job.json");job["success"]=success;job["cancelled"]=m_cancelled;
    job["elapsedSeconds"]=m_timer.elapsed()/1000.;job["backend"]=m_backend;job["output"]=output;job["exitCode"]=code;
    if(m_remoteJob){
        const auto remote=readJson(m_jobDir+"/remote-state.json");
        job["clientWaitSeconds"]=m_timer.elapsed()/1000.;
        if(remote.contains("elapsedSeconds"))job["elapsedSeconds"]=remote["elapsedSeconds"];
        job["remoteStatus"]=remote["status"];job["remoteJobId"]=QFileInfo(m_jobDir).fileName();
    }
    if(m_kind=="page-video")job["route"]=m_narrationRoute;
    writeJson(m_jobDir+"/job.json",job);
    if(success){m_status=QString("已完成 · %1 秒").arg(job["elapsedSeconds"].toDouble(),0,'f',1);m_progress=1;refreshHistory();}
    else if(m_cancelled){m_status="任务已取消";m_progress=0;}
    else{m_error=m_workerError.isEmpty()?"图像或媒体引擎未完成任务，请展开运行日志查看原因。":m_workerError;m_status="生成失败";}
    m_diagnostics.finish(success,m_cancelled,code,state==QProcess::CrashExit,m_error,readJson(m_jobDir+"/result.json"));
    m_remoteJob=false;
    if(success&&m_audioPipeline&&m_kind!="page-video"){
        persist();advanceAudioPipeline();
        if(busy())return;
        success=!m_audioPipeline&&m_error.isEmpty();
    }
    m_audioPipeline=false;
    if(success&&m_kind=="timeline"&&!m_timelineExport.isEmpty()){
        exportFile(output,m_timelineExport);success=m_error.isEmpty();m_timelineExport=QUrl();
    }
    emit changed();emit jobFinished(success);
}
void Studio::cancel(){
    if(m_remoteJob){m_audioPipeline=false;m_remote.cancel();return;}
    m_cancelled=true;m_audioPipeline=false;
    if(m_modelManager.busy()){m_modelManager.cancel();return;}
    if(m_process.state()!=QProcess::NotRunning){
        m_status="正在取消…";m_process.terminate();
        const auto pid=m_process.processId();
        QTimer::singleShot(3000,this,[this,pid]{if(m_process.processId()==pid&&m_process.state()!=QProcess::NotRunning)m_process.kill();});
        emit changed();
    }
}
void Studio::saveProject(QUrl url){
    auto project=m_doc.json();project["generation"]=recipe();
    project["image"]=m_pageImage;project["edit"]=m_edit.snapshot();project["repainting"]=m_repainting;
    project["narrating"]=m_narrating;project["audio"]=m_pageAudio;project["video"]=m_pageVideo;
    project["visual"]=m_pageVisual;project["narrationRoute"]=m_narrationRoute;
    project["sound"]=m_sound.json();project["timeline"]=m_timeline.snapshot();
    project["canvasLayers"]=m_layers.json();
    if(auto thread=m_conversations.value(m_pages.value(m_currentPage).toMap()["id"].toString()))project["conversation"]=thread->json();
    if(!ConversationMediaStore::pin(project.toVariantMap())){fail("无法保留工程引用的素材");return;}
    if(!writeJson(url.toLocalFile(),project))fail("保存工程失败");else{m_status="工程已保存";emit changed();}
}
void Studio::loadProject(QUrl url){
    if(busy())return;QString error;const auto project=readJson(url.toLocalFile());
    Document validator;
    if(!validator.load(project,&error)){fail(error);return;}
    LayerDocument canvasValidator;
    if(project.contains("canvasLayers")&&!canvasValidator.restoreSnapshot(project["canvasLayers"].toObject())){
        fail(canvasValidator.message());return;
    }
    capturePage();if(!savePages()){emit changed();return;}
    auto page=makePage(project,project["generation"].toObject(),project["image"].toString());
    if(project.contains("name"))page["name"]=project["name"].toString();
    page["edit"]=project["edit"].toObject().toVariantMap();page["repainting"]=project["repainting"].toBool();
    for(const auto &key:{"narrating","audio","video","visual","narrationRoute","sound","timeline","canvasLayers","conversation"})page[key]=project[key].toVariant();
    m_pages.append(page);
    activatePage(m_pages.size()-1);m_status="工程已在新创作页中打开";savePages();emit changed();
}
void Studio::exportResult(QUrl url,bool video){
    exportFile(local(video?m_video:m_image),url);
}
void Studio::exportEditResult(QUrl url){exportFile(local(m_edit.resultUrl()),url);}
void Studio::exportFile(QString src,QUrl url){
    QString dest=url.toLocalFile();if(src.isEmpty()||dest.isEmpty())return;
    if(QFileInfo(src).absoluteFilePath()==QFileInfo(dest).absoluteFilePath())return;
    QString error;
    if(!copyToUserSelectedFile(src,dest,&error)){
        fail("无法导出文件："+error);emit fileExportFinished(false,error);return;
    }
    m_error.clear();m_status="已导出 "+QFileInfo(dest).fileName();emit changed();
    emit fileExportFinished(true,dest);
}
void Studio::refreshHistory(){
    m_history.clear();
    QStringList roots=m_remote.archiveRoots();roots.prepend(m_outputRoot);roots.removeDuplicates();
    for(const auto &root:roots){QDir d(root);
    for(auto name:d.entryList(QDir::Dirs|QDir::NoDotAndDotDot,QDir::Name|QDir::Reversed)){
        auto job=readJson(d.filePath(name+"/job.json"));if(!job["success"].toBool())continue;
        if(job["kind"]=="voice"||job["kind"]=="sfx"||job["kind"]=="timeline")continue;
        auto item=job.toVariantMap();item["directory"]=d.filePath(name);item["url"]=QUrl::fromLocalFile(job["output"].toString()).toString();
        item["title"]=name; m_history.append(item);
    }}
    std::sort(m_history.begin(),m_history.end(),[](const QVariant &a,const QVariant &b){
        return a.toMap()["title"].toString()>b.toMap()["title"].toString();
    });
    emit changed();
}
void Studio::selectHistory(int i){
    if(busy()||i<0||i>=m_history.size())return;auto item=m_history[i].toMap();
    if(item["kind"]=="page-video"||item["kind"]=="avatar"){
        capturePage();if(!savePages())return;
        int target=-1;
        for(int n=0;n<m_pages.size();++n)if(m_pages[n].toMap()["videoSourceJob"]==item["directory"]){target=n;break;}
        if(target<0){
            const auto directory=item["directory"].toString();
            auto page=readJson(directory+"/page.json").toVariantMap();
            if(page.isEmpty()){
                Document blank;page=makePage(blank.json(),QJsonObject::fromVariantMap(item));
                const auto request=readJson(directory+"/request.json");
                page["audio"]=request["audio"].toString();page["visual"]=QUrl::fromLocalFile(request["image"].toString()).toString();
                page["narrationRoute"]="portrait";
            }
            page["id"]=QUuid::createUuid().toString(QUuid::WithoutBraces);
            page["video"]=item["url"];page["videoSourceJob"]=item["directory"];page["narrating"]=true;
            m_pages.append(page);target=m_pages.size()-1;
        }
        activatePage(target);m_narrating=true;m_repainting=false;savePages();
    }else if(item["kind"]=="edit"){
        capturePage();if(!savePages())return;
        int target=-1;
        for(int n=0;n<m_pages.size();++n)if(m_pages[n].toMap()["editSourceJob"]==item["directory"]){target=n;break;}
        if(target<0){
            EditDocument restored;const auto dir=item["directory"].toString();
            const auto error=restored.load(dir+"/source.png");
            if(!error.isEmpty()){fail(error);return;}
            restored.restoreMask(dir+(QFileInfo::exists(dir+"/local-selection.png")?"/local-selection.png":"/selection.png"));restored.m_prompt=item["prompt"].toString();
            restored.m_strength=item["strength"].toDouble();restored.m_feather=item["feather"].toInt();
            restored.m_blendMode=item["blendMode"].toString()=="direct"?"direct":"feather";
            restored.m_scope=item["scope"].toString()=="whole"?"whole":"local";
            restored.m_editMethod=item["editMethod"].toString()=="flowedit"?"flowedit":"standard";
            restored.m_sourcePrompt=item["sourcePrompt"].toString();
            restored.m_flowStrength=item.value("flowStrength",20.0/28.0).toDouble();
            restored.setResult(local(item["url"].toString()));
            Document blank;
            auto page=makePage(blank.json(),QJsonObject::fromVariantMap(item));
            page["edit"]=restored.snapshot().toVariantMap();page["repainting"]=true;page["editSourceJob"]=item["directory"];
            m_pages.append(page);target=m_pages.size()-1;
        }
        activatePage(target);m_repainting=true;savePages();
    }else if(item["kind"]=="image"){
        capturePage();
        int target=-1;
        for(int n=0;n<m_pages.size();++n)if(m_pages[n].toMap()["sourceJob"]==item["directory"]){target=n;break;}
        if(target<0){
            const auto project=readJson(item["directory"].toString()+"/project.json");Document validator;QString error;
            if(!validator.load(project,&error)){fail(error);return;}
            auto page=makePage(project,project.contains("generation")?project["generation"].toObject():QJsonObject::fromVariantMap(item),item["url"].toString());
            page["sourceJob"]=item["directory"];m_pages.append(page);target=m_pages.size()-1;
        }
        activatePage(target);savePages();
    }
    else m_video=item["url"].toString();
    m_status="已打开历史作品";emit changed();
}
void Studio::useGeneratedPortrait(){if(!m_image.isEmpty()){m_portrait=local(m_image);emit changed();}}
void Studio::setModelRoot(QUrl url){if(!busy()&&url.isLocalFile())m_modelManager.setRoot(url.toLocalFile());}
void Studio::openOutputs(){QDesktopServices::openUrl(QUrl::fromLocalFile(m_outputRoot));}
void Studio::downloadModels(){if(!busy())m_modelManager.requestDownload("image");}
