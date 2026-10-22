#include "Studio.h"
#include "ConversationMediaStore.h"
#include "UserSelectedFileExport.h"
#include <QDateTime>
#include <QDir>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTimer>
#include <QUuid>

namespace {
QString disk(QString path){return path.startsWith("file:")?QUrl(path).toLocalFile():path;}
bool write(QString path,const QJsonObject &value){QSaveFile file(path);const auto bytes=QJsonDocument(value).toJson();return file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size()&&file.commit();}
}
ConversationDocument *Studio::conversationFor(QString id){
    if(auto cached=m_conversations.value(id))return cached;
    QVariantMap page;for(const auto &value:m_pages)if(value.toMap()["id"]==id){page=value.toMap();break;}
    if(page.isEmpty())return nullptr;
    const auto json=QJsonObject::fromVariantMap(page["conversation"].toMap());
    auto thread=new ConversationDocument(id,m_outputRoot,json,this);m_conversations[id]=thread;
    if(!json.contains("editScope")&&!page["edit"].toMap()["source"].toString().isEmpty())
        thread->setEditScope(page["edit"].toMap()["scope"]=="whole"?"whole":"local");
    if(json.isEmpty()){
        // Old pages become works with their real results. Their source projects
        // and media are retained, without inventing a historical conversation.
        QStringList ids;const auto visual=page["visual"].toString().isEmpty()?page["image"].toString():page["visual"].toString();
        if(!visual.isEmpty())ids<<thread->addAsset(visual,"image","原作品画面");
        const auto settings=page["sound"].toMap();const auto speech=settings["voiceSource"]=="import"?page["audio"].toString():settings["speechResult"].toString();
        const auto voice=thread->addAsset(speech,"audio","原作品配音");if(!voice.isEmpty())ids<<voice;
        const auto video=thread->addAsset(page["video"].toString(),"video","原作品视频",visual,voice);if(!video.isEmpty())ids<<video;
        const auto sfx=thread->addAsset(settings["sfxResult"].toString(),"audio","原作品音效");if(!sfx.isEmpty())ids<<sfx;
        ids.removeAll(QString());if(!ids.isEmpty())thread->addResults(ids,"import","原作品素材");
        thread->setDraft(page["document"].toMap()["prompt"].toString());thread->setVoiceId(settings["voiceId"].toString());thread->setVoiceOptions({{"emotion",settings["emotion"]},{"dialect",settings["dialect"]}});
    }
    if(thread->voiceId().isEmpty())thread->setVoiceId(m_sound.settings()["voiceId"].toString());
    auto saveTimer=new QTimer(thread);saveTimer->setInterval(400);saveTimer->setSingleShot(true);
    connect(thread,&ConversationDocument::edited,saveTimer,qOverload<>(&QTimer::start));
    connect(saveTimer,&QTimer::timeout,this,[this,thread]{
        for(int i=0;i<m_pages.size();++i){auto row=m_pages[i].toMap();if(row["id"]!=thread->id())continue;
            row["conversation"]=thread->json().toVariantMap();row["modified"]=QDateTime::currentDateTime().toString(Qt::ISODate);
            m_pages[i]=row;break;}
        savePages();emit pagesChanged();
    });
    return thread;
}
void Studio::openConversation(QString id){
    auto thread=conversationFor(id);if(!thread||thread==m_conversation)return;
    m_conversation=thread;emit conversationChanged();
    // Only an inexpensive pointer selection is needed. It remains legal while
    // another work's inference process owns m_currentPage and its documents.
}
void Studio::newConversation(){
    Document blank;auto page=makePage(blank.json(),recipe());page["name"]="未命名作品";
    m_pages.append(page);openConversation(page["id"].toString());savePages();emit pagesChanged();
}
bool Studio::prepareConversationSettings(){
    if(busy()||!m_conversation)return false;
    for(int i=0;i<m_pages.size();++i)if(m_pages[i].toMap()["id"]==m_conversation->id()){
        if(i!=m_currentPage){capturePage();return activatePage(i);}return true;
    }
    return false;
}
bool Studio::prepareConversationEdit(){
    if(!prepareConversationSettings())return false;
    const auto target=m_conversation->target();const auto path=disk(target["url"].toString());
    if(!QImageReader(path).canRead()){m_conversation->report("请先选择可读取的操作图片");return false;}
    if(m_edit.sourcePath()!=path){
        const auto error=m_edit.load(path);if(!error.isEmpty()){m_conversation->report(error);return false;}
    }
    m_editConversationTarget=target["id"].toString();
    m_edit.setProperty("scope",m_conversation->editScope());
    return true;
}
void Studio::sendConversation(){
    auto thread=m_conversation;if(!thread)return;
    if(busy()){thread->report("另一个创作任务正在运行，可继续编辑，完成后再发送。");return;}
    QStringList needed;const auto requestedTask=thread->task();
    if((requestedTask=="image"||requestedTask=="edit")&&m_imageDevice=="local")needed<<"image";
    if(requestedTask=="voice"||(requestedTask=="avatar"&&thread->audio().isEmpty()))needed<<"voice";
    if(requestedTask=="avatar")needed<<"ditto";
    if(requestedTask=="sfx")needed<<"sfx";
    if(!needed.isEmpty()&&!ensureModels(needed))return;
    if(!thread->beginRequest())return;
    // Capture the engine's previous work before selecting the request owner.
    capturePage();int index=-1;for(int i=0;i<m_pages.size();++i)if(m_pages[i].toMap()["id"]==thread->id()){index=i;break;}
    if(index<0){thread->finishRequest(false,{},"作品已不存在");return;}
    const auto savedEdit=m_edit.snapshot();const auto editId=m_editConversationTarget;
    if(!activatePage(index)){thread->finishRequest(false,{},m_error);return;}
    if(editId==thread->target()["id"].toString())m_edit.restoreSnapshot(savedEdit);
    m_conversationJob=thread;m_conversationTask=thread->task();m_conversationTarget=thread->target();m_conversationClips=thread->clips();m_cancelled=false;m_audioPipeline=false;m_trackGeneration.clear();m_error.clear();
    const auto target=thread->target();const auto task=m_conversationTask;
    if(task=="pack")packageConversation();
    else if(task=="image"){
        m_doc.setPrompt(thread->draft());generate();
    }else if(task=="edit"){
        if(prepareConversationEdit()){
            m_edit.m_prompt=thread->draft();generateEdit();
        }else fail(thread->error());
    }else{
        m_sound.set("sfxEnabled",false);
        if(task=="sfx"){
            m_sound.set("voiceSource","none");m_sound.set("sfxPrompt",thread->draft());m_sound.set("sfxDuration",thread->sfxDuration());
            m_sound.setResult("sfxResult",{});generateTrack("sfx");
        }else{
            if(task=="avatar"){
                m_layers.clear();importTrackImage("image",QUrl(target["url"].toString()));
                if(!m_error.isEmpty()){finishConversation(false);return;}
            }
            if(task=="avatar"&&!thread->audio().isEmpty())setPageAudio(thread->audio()["url"].toString());
            else{
                m_sound.set("emotion",thread->voiceOptions()["emotion"]);m_sound.set("dialect",thread->voiceOptions()["dialect"]);
                m_sound.set("voiceSource","tts");m_sound.set("speechText",thread->draft());
                if(!thread->voiceId().isEmpty())m_sound.set("voiceId",thread->voiceId());
                m_sound.setResult("speechResult",{});
            }
            if(m_error.isEmpty()){if(task=="voice")generateTrack("voice");else generatePageVideo();}
        }
    }
    if(!m_busy&&m_conversationJob)finishConversation(m_error.isEmpty());
}
void Studio::finishConversation(bool success){
    auto thread=m_conversationJob;if(!thread)return;const auto task=m_conversationTask;
    m_conversationJob=nullptr;m_conversationTask.clear();QStringList ids;
    if(success){
        if(task=="image"||task=="edit")ids<<thread->addAsset(m_image,"image",task=="image"?"生成图像":"编辑结果");
        else if(task=="voice"||task=="sfx")ids<<thread->addAsset(m_sound.settings()[task=="voice"?"speechResult":"sfxResult"].toString(),"audio",task=="voice"?"合成配音":"生成音效");
        else if(task=="avatar"){
            const auto audio=thread->addAsset(effectiveSpeech(),"audio","数字人配音");
            ids<<thread->addAsset(m_pageVideo,"video","说话视频",m_conversationTarget["url"].toString(),audio);if(!audio.isEmpty())ids<<audio;
        }else if(task=="pack"){
            QString poster;for(const auto &value:m_conversationClips){const auto clip=value.toMap();if(clip["lane"]=="visual"){const auto item=thread->asset(clip["assetId"].toString());poster=item["kind"]=="image"?item["url"].toString():item["poster"].toString();break;}}
            ids<<thread->addAsset(m_video,"video","打包成片",poster);
        }
        ids.removeAll(QString());if(ids.isEmpty())success=false;
    }
    for(const auto &id:ids)thread->markTemporary(id,m_jobDir);
    thread->finishRequest(success,ids,success?QString():m_cancelled?"任务已取消，可重新发送。":m_error.isEmpty()?"没有得到有效的生成结果，请查看日志。":m_error);
    for(int i=0;i<m_pages.size();++i){auto row=m_pages[i].toMap();if(row["id"]!=thread->id())continue;
        row["conversation"]=thread->json().toVariantMap();if(row["name"].toString().isEmpty()||row["name"]=="未命名作品"){
            for(const auto &message:thread->messages())if(message.toMap()["role"]=="user"){row["name"]=message.toMap()["text"].toString().trimmed().left(30);break;}
        }
        if(task=="image"||task=="edit")row["visual"]=m_image;
        m_pages[i]=row;break;}
    savePages();emit pagesChanged();
}
void Studio::packageConversation(){
    const auto worker=engine("moss-ditto");if(worker.isEmpty()){fail("未找到本地视频导出引擎");return;}
    const auto thread=m_conversationJob;QJsonArray tracks;
    QString lastVisual;double lastEnd=-1;
    for(const auto &value:thread->clips()){const auto clip=value.toMap();const auto end=clip["start"].toDouble()+clip["duration"].toDouble();if(clip["lane"]=="visual"&&end>lastEnd){lastEnd=end;lastVisual=clip["id"].toString();}}
    for(const auto &value:thread->clips()){
        const auto clip=value.toMap(),item=thread->asset(clip["assetId"].toString());
        const bool visual=clip["lane"]=="visual";
        QJsonObject row{{"key",visual?"image":"sfx"},{"kind",visual?item["kind"].toString():"audio"},
            {"source",disk(item["url"].toString())},{"start",clip["start"].toDouble()},{"duration",clip["duration"].toDouble()},
            {"x",0},{"y",0},{"width",1},{"height",1},{"gain",1},{"enabled",true}};
        // Long audio keeps the final picture/video frame on screen. Deliberate
        // gaps between clips remain gaps, as displayed in the simple timeline.
        if(clip["id"]==lastVisual)row["duration"]=thread->duration()-clip["start"].toDouble();
        tracks.append(row);
    }
    m_jobDir=m_outputRoot+"/"+QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss")+"-conversation-pack-"+QUuid::createUuid().toString(QUuid::Id128).left(6);
    if(!QDir().mkpath(m_jobDir)||!write(m_jobDir+"/request.json",{{"directory",m_jobDir},{"width",m_doc.canvasWidth()},{"height",m_doc.canvasHeight()},{"fps",25},{"duration",thread->duration()},{"tracks",tracks}})||
       !write(m_jobDir+"/job.json",{{"kind","timeline"},{"conversation",thread->id()},{"started",QDateTime::currentDateTime().toString(Qt::ISODate)}})){fail("无法保存视频打包任务");return;}
    m_timelineExport=QUrl();start(worker,{"render-timeline",m_jobDir+"/request.json"},"timeline");
}
void Studio::exportConversationAsset(QString id,QUrl destination){if(m_conversation)exportFile(disk(m_conversation->asset(id)["url"].toString()),destination);}
void Studio::saveConversation(QUrl destination){
    if(!m_conversation)return;capturePage();QJsonObject project;for(const auto &value:m_pages){const auto row=value.toMap();if(row["id"]==m_conversation->id()){project=QJsonObject::fromVariantMap(row["document"].toMap());for(auto it=row.begin();it!=row.end();++it)if(it.key()!="document")project[it.key()]=QJsonValue::fromVariant(it.value());break;}}
    project["conversation"]=m_conversation->json();
    QString error;
    if(!ConversationMediaStore::pin(project.toVariantMap())) error="无法保留工程引用的素材";
    else if(!destination.isLocalFile()) error="请选择本机文件";
    else if(writeToUserSelectedFile(QJsonDocument(project).toJson(),destination.toLocalFile(),&error)) {
        emit conversationSaveFinished(true,destination.toLocalFile());return;
    }
    fail("保存作品工程失败："+error);
    emit conversationSaveFinished(false,error);
}
QVariantList Studio::layoutTemplates() const {
    QVariantList rows;
    for(const auto &key:{"poster","note","objects"}){
        Document document;document.loadTemplate(key);
        rows.append(QVariantMap{{"id",key},{"name",QString(key)=="poster"?"春日海报":QString(key)=="note"?"手写便签":"物品构图"},{"builtin",true},{"document",document.json().toVariantMap()}});
    }
    QFile file(m_outputRoot+"/library/layout-templates.json");if(file.open(QIODevice::ReadOnly))rows+=QJsonDocument::fromJson(file.readAll()).object()["templates"].toArray().toVariantList();
    return rows;
}
QString Studio::importLayoutTemplate(QUrl source){
    QFile file(source.toLocalFile());if(!source.isLocalFile()||!file.open(QIODevice::ReadOnly)||file.size()>4*1024*1024)return "请选择 4 MB 以内的构图或工程 JSON";
    const auto bytes=file.readAll();const auto json=QJsonDocument::fromJson(bytes).object();Document document;QString error;
    if(json.isEmpty())return "构图文件不是有效的 JSON";
    if(json.contains("summary")){error=document.importLayout(QString::fromUtf8(bytes),1024,1024);if(!error.isEmpty())return error;}
    else if(!document.load(json.contains("document")?json["document"].toObject():json,&error))return error;
    if(!document.layout())return "此工程没有构图区域，请选择包含构图数据的模板";
    auto rows=layoutTemplates();rows.erase(std::remove_if(rows.begin(),rows.end(),[](const QVariant &v){return v.toMap()["builtin"].toBool();}),rows.end());
    rows.append(QVariantMap{{"id",QUuid::createUuid().toString(QUuid::WithoutBraces)},{"name",QFileInfo(source.toLocalFile()).completeBaseName()},{"builtin",false},{"document",document.json().toVariantMap()}});
    QJsonObject catalog;QFile previous(m_outputRoot+"/library/layout-templates.json");if(previous.open(QIODevice::ReadOnly))catalog=QJsonDocument::fromJson(previous.readAll()).object();
    catalog["version"]=1;catalog["templates"]=QJsonArray::fromVariantList(rows);
    QDir().mkpath(m_outputRoot+"/library");if(!write(m_outputRoot+"/library/layout-templates.json",catalog))return "无法保存构图模板";
    emit templatesChanged();return {};
}
void Studio::removeLayoutTemplate(QString id){
    auto rows=layoutTemplates();QVariantList kept,archived;
    QFile file(m_outputRoot+"/library/layout-templates.json");if(file.open(QIODevice::ReadOnly))archived=QJsonDocument::fromJson(file.readAll()).object()["archived"].toArray().toVariantList();
    for(const auto &value:rows){const auto row=value.toMap();if(row["builtin"].toBool())continue;if(row["id"]==id)archived.append(row);else kept.append(row);}
    if(!write(m_outputRoot+"/library/layout-templates.json",{{"version",1},{"templates",QJsonArray::fromVariantList(kept)},{"archived",QJsonArray::fromVariantList(archived)}})){fail("无法更新模板库");return;}emit templatesChanged();
}
void Studio::useLayoutTemplate(QString id){
    if(busy())return;
    for(const auto &value:layoutTemplates()){const auto row=value.toMap();if(row["id"]!=id)continue;
        newConversation();for(int i=0;i<m_pages.size();++i){auto page=m_pages[i].toMap();if(page["id"]!=m_conversation->id())continue;
            page["document"]=row["document"];page["name"]=row["name"];m_pages[i]=page;break;}
        m_conversation->setDraft(row["document"].toMap()["prompt"].toString());savePages();emit pagesChanged();return;
    }
}

bool Studio::deleteConversationMessage(QString workId,QString messageId,bool cleanup){
    if(busy())return false;auto thread=conversationFor(workId);if(!thread||thread->pending())return false;
    capturePage();if(!savePages()){thread->report("作品保存失败，消息与素材均已保留。");return false;}
    // Persist the new history before changing the live object or deleting files.
    ConversationDocument candidate(workId,m_outputRoot,thread->json());QVariantList orphaned;
    if(!candidate.removeMessage(messageId,&orphaned))return false;
    auto pages=m_pages;for(auto &value:pages){auto page=value.toMap();if(page["id"]==workId){page["conversation"]=candidate.json().toVariantMap();value=page;break;}}
    if(!write(m_outputRoot+"/creation-pages.json",{{"version",5},{"current",m_currentPage},{"activeConversation",m_conversation?m_conversation->id():QString()},{"pages",QJsonArray::fromVariantList(pages)}})){
        thread->report("作品保存失败，消息与素材均已保留。");return false;
    }
    m_pages=pages;thread->removeMessage(messageId);emit pagesChanged();
    if(cleanup){
        QStringList directories;for(const auto &asset:orphaned)directories<<asset.toMap()["temporaryDirectory"].toString();
        auto roots=m_remote.archiveRoots();roots.prepend(m_outputRoot);
        // The edit canvas, timeline, other works and recoverable deleted work
        // are genuine consumers, not just the remaining chat messages.
        const auto failures=ConversationMediaStore::clean(directories,roots,QVariantList{m_pages,m_deletedPage});
        if(!failures.isEmpty())thread->report("消息已删除，部分临时文件未能清理。正在使用的素材仍保留。");
        refreshHistory();
    }
    m_status="消息已删除";emit changed();return true;
}
