#include "ConversationDocument.h"
#include "VoiceStyle.h"
#include "AudioDurationProbe.h"
#include "ConversationMediaStore.h"
#include <QSet>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QMediaPlayer>
#include <QMediaMetaData>
#include <QMimeDatabase>
#include <QSaveFile>
#include <QUuid>
#include <QTimer>
#include <cmath>

namespace {
QString uid(){ return QUuid::createUuid().toString(QUuid::WithoutBraces); }
QString local(QString path){ return path.startsWith("file:")?QUrl(path).toLocalFile():path; }
QString url(QString path){ return QUrl::fromLocalFile(local(path)).toString(); }
QString title(QString task){ return QVariantMap{{"image","生成图像"},{"edit","编辑图像"},{"avatar","让画面说话"},{"voice","语音合成"},{"sfx","音效生成"},{"pack","打包视频"}}.value(task).toString(); }
}
ConversationDocument::ConversationDocument(QString id,QString root,const QJsonObject &json,QObject *parent)
    :QObject(parent),m_id(id),m_root(root){
    m_messages=json["messages"].toArray().toVariantList();m_assets=json["assets"].toArray().toVariantList();
    m_drafts=json["drafts"].toObject().toVariantMap();m_targets=json["targets"].toObject().toVariantMap();
    m_voiceOptions=VoiceStyle::normalize(json["voiceOptions"].toObject().toVariantMap());
    m_audioId=json["audioId"].toString();m_voiceId=json["voiceId"].toString();
    m_selected=json["selected"].toArray().toVariantList();m_clips=json["clips"].toArray().toVariantList();
    m_manual=json["manual"].toBool();m_sfxDuration=std::clamp(json["sfxDuration"].toInt(5),1,30);
    m_editScope=json["editScope"].toString()=="local"?"local":"whole";
    setTask(json["task"].toString("image"));
    for(auto &value:m_messages){auto row=value.toMap();if(row["status"]=="running"){
        row["status"]="interrupted";row["text"]="上次任务已中断，文案和素材已保留，可重新发送。";value=row;}}
    if(json["version"].toInt()<2)for(int i=1;i<m_messages.size();++i){
        auto row=m_messages[i].toMap();const auto previous=m_messages[i-1].toMap();
        if(row["role"]=="assistant"&&row["task"]!="import"&&previous["role"]=="user"&&row["task"]==previous["task"]){row["replyTo"]=previous["id"];m_messages[i]=row;}
    }
    for(const auto &value:m_assets){auto row=value.toMap();if(row["kind"]!="image"&&row["duration"].toDouble()<=0)probe(row["id"].toString());}
}
void ConversationDocument::touch(){emit changed();emit edited();}
void ConversationDocument::report(QString text){m_error=text;emit changed();}
void ConversationDocument::dismissError(){m_error.clear();emit changed();}
void ConversationDocument::setTask(QString task){
    if(title(task).isEmpty()||m_task==task)return;m_task=task;m_error.clear();touch();
}
void ConversationDocument::setDraft(QString text){if(draft()==text)return;m_drafts[m_task]=text;touch();}
void ConversationDocument::setEditScope(QString scope){
    if((scope!="whole"&&scope!="local")||scope==m_editScope)return;
    m_editScope=scope;m_error.clear();touch();
}
void ConversationDocument::setVoiceOptions(QVariantMap options){options=VoiceStyle::normalize(options);if(m_voiceOptions==options)return;m_voiceOptions=options;touch();}
void ConversationDocument::setVoiceId(QString id){if(m_voiceId==id)return;m_voiceId=id;touch();}
void ConversationDocument::setSfxDuration(int seconds){seconds=std::clamp(seconds,1,30);if(seconds==m_sfxDuration)return;m_sfxDuration=seconds;touch();}
QVariantMap ConversationDocument::asset(QString id) const {for(const auto &value:m_assets)if(value.toMap()["id"]==id)return value.toMap();return {};}
QVariantMap ConversationDocument::target() const {
    const auto selected=asset(m_targets.value(m_task).toString());if(selected["kind"]=="image")return selected;
    for(auto it=m_assets.crbegin();it!=m_assets.crend();++it)if(it->toMap()["kind"]=="image")return it->toMap();return {};
}
void ConversationDocument::chooseTarget(QString id){
    if(!id.isEmpty()&&asset(id)["kind"]!="image")return;
    if(m_task!="edit"&&m_task!="avatar")m_task="edit";
    m_targets[m_task]=id;m_error.clear();touch();
}
void ConversationDocument::chooseAudio(QString id){
    if(!id.isEmpty()&&asset(id)["kind"]!="audio")return;m_audioId=id;m_error.clear();touch();
}
QString ConversationDocument::addAsset(QString path,QString kind,QString name,QString poster,QString audioId){
    path=local(path);if(!QFileInfo(path).isFile())return {};
    if(!name.isEmpty()){
        const auto base=name;int number=1;for(const auto &value:m_assets)if(value.toMap()["name"].toString().startsWith(base))++number;
        name+=QString(" %1").arg(number,2,10,QChar('0'));
    }
    const auto id=uid();
    QVariantMap row{{"id",id},{"kind",kind},{"name",name.isEmpty()?QFileInfo(path).fileName():name},
        {"url",url(path)},{"poster",poster},{"audioId",audioId},{"duration",kind=="image"?5.:AudioDurationProbe::waveDuration(path)},
        {"valid",true},{"hasAudio",!audioId.isEmpty()}};
    if(kind=="image"||!poster.isEmpty()){QImageReader reader(kind=="image"?path:local(poster));const auto size=reader.size();row["width"]=size.width();row["height"]=size.height();}
    const auto owner=ConversationMediaStore::ownedDirectory(path);if(!owner.isEmpty())row["temporaryDirectory"]=owner;
    m_assets.append(row);emit assetsChanged();emit changed();emit edited();
    if(kind!="image"&&row["duration"].toDouble()<=0)probe(id);
    return id;
}
void ConversationDocument::probe(QString id){
    const auto source=asset(id)["url"].toString();if(source.isEmpty())return;
    auto player=new QMediaPlayer(this);
    const auto update=[this,id,player](bool invalid){
        for(int i=0;i<m_assets.size();++i){auto row=m_assets[i].toMap();if(row["id"]!=id)continue;
            const auto resolution=player->metaData().value(QMediaMetaData::Resolution).toSize();
            if(resolution.isValid()){row["width"]=resolution.width();row["height"]=resolution.height();}
            if(player->duration()>0)row["duration"]=player->duration()/1000.;
            row["hasAudio"]=player->hasAudio()||!row["audioId"].toString().isEmpty();
            if(invalid){row["valid"]=false;report("无法读取媒体："+row["name"].toString());}
            m_assets[i]=row;emit assetsChanged();rebuildClips();emit changed();emit edited();break;}
    };
    connect(player,&QMediaPlayer::mediaStatusChanged,this,[player,update](QMediaPlayer::MediaStatus status){
        if(status==QMediaPlayer::LoadedMedia||status==QMediaPlayer::InvalidMedia){update(status==QMediaPlayer::InvalidMedia);player->deleteLater();}
    });
    // A broken file must not leave a metadata player alive indefinitely.
    QTimer::singleShot(15000,player,&QObject::deleteLater);
    player->setSource(QUrl(source));
}
QString ConversationDocument::importMedia(QUrl source,QString purpose){
    if(!source.isLocalFile()||!QFileInfo(source.toLocalFile()).isFile()){report("请选择本机可读取的素材文件");return {};}
    const auto path=source.toLocalFile(),mime=QMimeDatabase().mimeTypeForFile(path).name();
    const auto kind=QImageReader(path).canRead()?QString("image"):mime.startsWith("audio/")?QString("audio"):mime.startsWith("video/")?QString("video"):QString();
    if(kind.isEmpty()||(purpose=="target"&&kind!="image")||((purpose=="audio"||purpose=="sfx")&&kind!="audio")){report("请选择与当前功能匹配的图像、音频或视频");return {};}
    const auto directory=m_root+"/conversation-assets/"+uid();const auto dest=directory+"/"+QFileInfo(path).fileName();
    QFile input(path);QSaveFile output(dest);if(!QDir().mkpath(directory)||!input.open(QIODevice::ReadOnly)||!output.open(QIODevice::WriteOnly)){report("无法保存导入素材");return {};}
    while(!input.atEnd()){const auto data=input.read(1024*1024);if(data.isEmpty()||output.write(data)!=data.size()){report("保存导入素材失败");return {};}}
    if(!output.commit()){report("保存导入素材失败");return {};}
    const auto id=addAsset(dest,kind);markTemporary(id,directory);addResults({id},purpose=="sfx"?"sfx":"import","导入素材");
    if(purpose=="target")chooseTarget(id);else if(purpose=="audio")chooseAudio(id);else if(purpose=="pack")toggleSelected(id);
    m_error.clear();touch();return id;
}
void ConversationDocument::addResults(const QStringList &ids,QString task,QString text){
    if(ids.isEmpty())return; m_messages.append(QVariantMap{{"id",uid()},{"role","assistant"},{"task",task},{"text",text},{"assetIds",ids},{"status","complete"}});
    emit messagesChanged();emit edited();
}
double ConversationDocument::duration() const {double end=0;for(const auto &value:m_clips){const auto row=value.toMap();end=std::max(end,row["start"].toDouble()+row["duration"].toDouble());}return end;}
void ConversationDocument::rebuildClips(){
    QVariantList next;double visual=0,audio=0;
    auto append=[&](QString id,const QVariantMap &item,QString lane,double start,QString linked={}){
        next.append(QVariantMap{{"id",id},{"assetId",item["id"]},{"lane",lane},{"name",item["name"]},{"start",start},{"duration",item["duration"]},{"linkedTo",linked}});
    };
    for(const auto &value:m_selected){const auto item=asset(value.toString());if(item.isEmpty()||item["kind"]=="audio")continue;
        append("clip-"+value.toString(),item,"visual",visual);
        if(item["kind"]=="video"&&item["hasAudio"].toBool()&&!m_selected.contains(item["audioId"])){
            auto sound=asset(item["audioId"].toString());if(sound.isEmpty())sound=item;
            sound["name"]="原声 · "+item["name"].toString();append("native-"+value.toString(),sound,"audio",visual,value.toString());
            audio=std::max(audio,visual+sound["duration"].toDouble());
        }
        visual+=item["duration"].toDouble();
    }
    for(const auto &value:m_selected){const auto item=asset(value.toString());if(item["kind"]!="audio")continue;append("clip-"+value.toString(),item,"audio",audio);audio+=item["duration"].toDouble();}
    if(m_manual)for(auto &value:next){auto row=value.toMap();for(const auto &old:m_clips){const auto prior=old.toMap();if(prior["id"]==row["id"]){row["start"]=prior["start"];row["linkedTo"]=prior["linkedTo"];break;}}value=row;}
    m_clips=next;emit clipsChanged();
}
void ConversationDocument::toggleSelected(QString id){
    if(asset(id).isEmpty())return;
    if(m_selected.contains(id))m_selected.removeAll(id);else{if(m_selected.size()>=24){report("一次最多选择 24 项素材");return;}m_selected.append(id);}
    rebuildClips();emit edited();
}
void ConversationDocument::moveClip(QString id,double start){
    if(!std::isfinite(start))return;
    QString source;double delta=0;
    for(auto &value:m_clips){auto row=value.toMap();if(row["id"]!=id)continue;
        const double next=std::clamp(std::round(start*10)/10,0.,std::max(0.,600-row["duration"].toDouble()));
        delta=next-row["start"].toDouble();row["start"]=next;
        if(row["lane"]=="audio")row.remove("linkedTo");else source=row["assetId"].toString();value=row;break;
    }
    if(!source.isEmpty())for(auto &value:m_clips){auto row=value.toMap();if(row["linkedTo"]==source){row["start"]=std::max(0.,row["start"].toDouble()+delta);value=row;}}
    m_manual=true;emit clipsChanged();emit edited();
}
void ConversationDocument::arrange(){m_manual=false;rebuildClips();emit edited();}
bool ConversationDocument::beginRequest(){
    if(pending())return false;QString problem;QStringList refs;
    if(m_task=="pack"){
        bool visual=false;for(const auto &value:m_clips){const auto clip=value.toMap(),item=asset(clip["assetId"].toString());visual|=clip["lane"]=="visual";
            if(!item["valid"].toBool()||clip["duration"].toDouble()<=0||!QFileInfo(local(item["url"].toString())).isFile())problem="部分素材无法读取或尚未取得时长，请重新导入。";}
        if(!visual)problem="请至少选择一张图片或一段视频。";
        if(duration()>600)problem="打包视频最长支持十分钟。";
        for(const auto &value:m_selected)refs<<value.toString();
    }else{
        if((m_task=="edit"||m_task=="avatar")&&(target().isEmpty()||!QFileInfo(local(target()["url"].toString())).isFile()))problem="请先选择可读取的操作图片。";
        const bool audioSelected=m_task=="avatar"&&!audio().isEmpty();
        if(audioSelected&&(!audio()["valid"].toBool()||!QFileInfo(local(audio()["url"].toString())).isFile()))problem="所选配音不可用，请重新导入。";
        if(draft().trimmed().isEmpty()&&!audioSelected)problem="请先输入创作内容。";
        if(m_task=="edit"||m_task=="avatar")refs<<target()["id"].toString();
        if(audioSelected)refs<<m_audioId;
    }
    if(!problem.isEmpty()){report(problem);return false;}
    m_error.clear();m_pending=uid();m_pendingTask=m_task;
    const auto text=m_task=="pack"?QString("打包为视频 · %1 项素材").arg(m_selected.size()):m_task=="avatar"&&!audio().isEmpty()?QString("用所选配音驱动这张图像。"):draft().trimmed();
    const auto requestId=uid();
    m_messages.append(QVariantMap{{"id",requestId},{"role","user"},{"text",text},{"task",m_task},{"editScope",m_editScope},{"refs",refs},{"voiceId",m_voiceId},{"voiceOptions",m_voiceOptions}});
    m_messages.append(QVariantMap{{"id",m_pending},{"replyTo",requestId},{"role","assistant"},{"task",m_task},{"status","running"},{"assetIds",QStringList{}}});
    emit messagesChanged();emit edited();return true;
}
void ConversationDocument::finishRequest(bool success,const QStringList &ids,QString error){
    if(!pending())return;
    for(auto &value:m_messages){auto row=value.toMap();if(row["id"]!=m_pending)continue;
        row["status"]=success?"complete":"failed";row["assetIds"]=ids;row["text"]=success?QString():error;value=row;break;}
    m_pending.clear();m_pendingTask.clear();emit messagesChanged();emit edited();
}
QJsonObject ConversationDocument::json() const {
    return {{"version",2},{"task",m_task},{"editScope",m_editScope},{"drafts",QJsonObject::fromVariantMap(m_drafts)},{"targets",QJsonObject::fromVariantMap(m_targets)},
        {"audioId",m_audioId},{"voiceId",m_voiceId},{"voiceOptions",QJsonObject::fromVariantMap(m_voiceOptions)},{"sfxDuration",m_sfxDuration},{"selected",QJsonArray::fromVariantList(m_selected)},
        {"clips",QJsonArray::fromVariantList(m_clips)},{"manual",m_manual},{"messages",QJsonArray::fromVariantList(m_messages)},{"assets",QJsonArray::fromVariantList(m_assets)}};
}

void ConversationDocument::markTemporary(QString id,QString directory){
    const auto root=ConversationMediaStore::canonical(directory);
    const auto path=ConversationMediaStore::canonical(asset(id)["url"].toString());
    if(root.isEmpty()||!path.startsWith(root+"/")||!ConversationMediaStore::mark(root))return;
    for(auto &value:m_assets){auto row=value.toMap();if(row["id"]==id){row["temporaryDirectory"]=root;value=row;emit assetsChanged();emit edited();break;}}
}
bool ConversationDocument::removeMessage(QString id,QVariantList *orphaned){
    if(pending())return false;
    int index=-1;for(int i=0;i<m_messages.size();++i)if(m_messages[i].toMap()["id"]==id){index=i;break;}
    if(index<0)return false;m_messages.removeAt(index);
    QSet<QString> live;
    for(const auto &value:m_messages){const auto message=value.toMap();for(const auto &key:{"assetIds","refs"})for(const auto &ref:message[key].toStringList())live.insert(ref);}
    // Retain audio and poster dependencies even after their original cards have
    // been removed. Later requests also retain their exact source assets.
    bool added=true;while(added){added=false;
        for(const auto &value:m_assets){const auto row=value.toMap();if(!live.contains(row["id"].toString()))continue;
            QStringList dependencies{row["audioId"].toString()};
            const auto poster=ConversationMediaStore::canonical(row["poster"].toString());
            if(!poster.isEmpty())for(const auto &other:m_assets)if(ConversationMediaStore::canonical(other.toMap()["url"].toString())==poster)dependencies<<other.toMap()["id"].toString();
            for(const auto &dep:dependencies)if(!dep.isEmpty()&&!live.contains(dep)){live.insert(dep);added=true;}
        }
    }
    QVariantList kept;for(const auto &value:m_assets){const auto key=value.toMap()["id"].toString();if(live.contains(key))kept<<value;else{
        if(orphaned)orphaned->append(value);m_selected.removeAll(key);
        if(m_audioId==key)m_audioId.clear();for(auto it=m_targets.begin();it!=m_targets.end();++it)if(it.value()==key)it.value()=QString();
    }}
    m_assets=kept;m_error.clear();rebuildClips();emit assetsChanged();emit messagesChanged();touch();return true;
}
