#include "SoundPlan.h"
#include "VoiceStyle.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QSaveFile>
#include <QUuid>
#include <QtEndian>

namespace {
QJsonObject read(QString path){QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};return QJsonDocument::fromJson(f.readAll()).object();}
}
void SoundPlan::initialize(QString root){m_outputRoot=root;loadVoices();}
void SoundPlan::restore(QJsonObject value){
    m_settings={{"voiceSource","none"},{"voiceId",""},{"speechText",""},{"sfxEnabled",false},
        {"sfxPrompt",""},{"sfxDuration",5},{"sfxGain",.35},{"speechResult",""},{"sfxResult",""},{"mouthReference","auto"},{"emotion","中性"},{"dialect","standard"}};
    for(auto it=value.begin();it!=value.end();++it)if(m_settings.contains(it.key()))m_settings[it.key()]=it.value().toVariant();
    if(!QStringList{"none","import","tts"}.contains(m_settings["voiceSource"].toString()))m_settings["voiceSource"]="none";
    if(!QStringList{"auto","source"}.contains(m_settings["mouthReference"].toString()))m_settings["mouthReference"]="auto";
    const auto style=VoiceStyle::normalize(m_settings);
    m_settings["emotion"]=style["emotion"];m_settings["dialect"]=style["dialect"];
    m_settings["sfxDuration"]=std::clamp(m_settings["sfxDuration"].toInt(),1,30);
    m_settings["sfxGain"]=std::clamp(m_settings["sfxGain"].toDouble(),0.,1.);
    if(selectedVoice().isEmpty()&&!m_voices.isEmpty())m_settings["voiceId"]=m_voices.first().toMap()["id"];
    emit changed();
}
void SoundPlan::set(QString key,QVariant value){
    if(!m_settings.contains(key)||key.endsWith("Result"))return;
    if(key=="voiceSource"&&!QStringList{"none","import","tts"}.contains(value.toString()))return;
    if(key=="mouthReference"&&!QStringList{"auto","source"}.contains(value.toString()))return;
    if(key=="emotion"&&!VoiceStyle::emotions().contains(value.toString()))return;
    if(key=="dialect"&&!VoiceStyle::dialects().contains(value.toString()))return;
    if(key=="sfxDuration")value=std::clamp(value.toInt(),1,30);
    if(key=="sfxGain")value=std::clamp(value.toDouble(),0.,1.);
    if(m_settings[key]==value)return;
    m_settings[key]=value;
    if(key=="emotion"&&value!="中性")m_settings["dialect"]="standard";
    if(key=="dialect"&&value!="standard")m_settings["emotion"]="中性";
    if(key=="speechText"||key=="voiceId"||key=="emotion"||key=="dialect")m_settings["speechResult"]="";
    if(key=="sfxPrompt"||key=="sfxDuration")m_settings["sfxResult"]="";
    emit edited();emit settingEdited(key);emit changed();
}
void SoundPlan::setResult(QString key,QString value){if(key=="speechResult"||key=="sfxResult"){m_settings[key]=value;emit changed();}}
QVariantMap SoundPlan::selectedVoice()const{
    for(const auto &v:m_voices)if(v.toMap()["id"]==m_settings["voiceId"])return v.toMap();
    return {};
}
void SoundPlan::loadVoices(){
    m_voices.clear();
    QString root=QCoreApplication::applicationDirPath()+"/../Resources/assets/voices";
#ifdef Q_OS_WIN
    root=QCoreApplication::applicationDirPath()+"/assets/voices";
#endif
    if(!QFileInfo::exists(root+"/catalog.json")&&!qEnvironmentVariableIsSet("MOSS_DISABLE_DEV_PATHS"))
        root=QString(MOSS_SOURCE_ROOT)+"/assets/voices";
    for(const auto &value:read(root+"/catalog.json")["presets"].toArray()){
        const auto row=value.toObject();
        const auto file=root+"/"+QFileInfo(row["audio_asset"].toString()).fileName();
        if(!QFileInfo::exists(file))continue;
        m_voices.append(QVariantMap{{"id",row["id"].toString()},{"name",row["fallback_name"].toString()},
            {"reference",file},{"transcript",row["reference_transcript"].toString()},
            {"language",row["language"].toString()},{"builtin",true}});
    }
    const auto custom=read(m_outputRoot+"/voices/catalog.json")["voices"].toArray();
    for(const auto &value:custom){auto row=value.toObject().toVariantMap();if(QFileInfo::exists(row["reference"].toString()))m_voices.append(row);}
    if(selectedVoice().isEmpty()&&!m_voices.isEmpty())m_settings["voiceId"]=m_voices.first().toMap()["id"];
    emit voicesChanged();emit changed();
}
QString SoundPlan::createVoice(QString name,QUrl url,QString transcript,QVariantMap designRecipe){
    if(name.trimmed().isEmpty()||transcript.trimmed().isEmpty())return "请填写音色名称和录音对应文字";
    QFile input(url.toLocalFile());if(!input.open(QIODevice::ReadOnly)||input.size()>20*1024*1024)return "请选择 20 MB 以内的 WAV 参考录音";
    const auto header=input.read(12);
    if(header.left(4)!="RIFF"||header.mid(8,4)!="WAVE")return "请选择 WAV 格式的参考录音";
    quint32 rate=0,data=0;
    while(input.pos()+8<=input.size()){
        const auto chunk=input.read(8);const auto size=qFromLittleEndian<quint32>(chunk.constData()+4);
        const auto next=input.pos()+qint64(size)+(size&1);
        if(next>input.size())return "参考录音不完整";
        if(chunk.left(4)=="fmt "&&size>=16){const auto format=input.read(16);rate=qFromLittleEndian<quint32>(format.constData()+8);}
        if(chunk.left(4)=="data")data=size;
        input.seek(next);
    }
    if(!rate||double(data)/rate<3||double(data)/rate>30)return "请使用 3–30 秒的清晰参考录音";
    const auto id=QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto directory=m_outputRoot+"/voices";const auto file=directory+"/"+id+".wav";
    if(!QDir().mkpath(directory)||!QFile::copy(url.toLocalFile(),file))return "无法保存参考音色";
    auto list=read(directory+"/catalog.json")["voices"].toArray();
    QJsonObject row{{"id",id},{"name",name.trimmed()},{"reference",file},{"transcript",transcript.trimmed()},{"builtin",false}};
    if(!designRecipe.isEmpty()){row["origin"]="ming-voice-design";row["designRecipe"]=QJsonObject::fromVariantMap(designRecipe);row["synthetic"]=true;}
    list.append(row);
    QSaveFile save(directory+"/catalog.json");
    auto catalog=read(directory+"/catalog.json");catalog["voices"]=list;
    const auto bytes=QJsonDocument(catalog).toJson();
    if(!save.open(QIODevice::WriteOnly)||save.write(bytes)!=bytes.size()||!save.commit()){QFile::remove(file);return "无法保存音色库";}
    loadVoices();set("voiceId",id);set("voiceSource","tts");return {};
}
namespace {
bool saveVoiceCatalog(const QString &path,const QJsonObject &catalog){
    QSaveFile file(path);const auto bytes=QJsonDocument(catalog).toJson();
    return file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size()&&file.commit();
}
}
bool SoundPlan::canRestoreVoice()const{return !read(m_outputRoot+"/voices/catalog.json")["archived"].toArray().isEmpty();}
QString SoundPlan::updateVoice(QString id,QString name,QString transcript){
    if(name.trimmed().isEmpty()||name.trimmed().size()>120||transcript.trimmed().isEmpty())return "请填写音色名称和参考文字";
    const auto path=m_outputRoot+"/voices/catalog.json";auto catalog=read(path);auto voices=catalog["voices"].toArray();
    for(int i=0;i<voices.size();++i){auto row=voices[i].toObject();if(row["id"]!=id)continue;
        row["name"]=name.trimmed();row["transcript"]=transcript.trimmed();voices[i]=row;catalog["voices"]=voices;
        if(!saveVoiceCatalog(path,catalog))return "无法保存音色库";
        if(m_settings["voiceId"]==id)m_settings["speechResult"]="";
        loadVoices();emit edited();emit settingEdited("voiceId");return {};
    }return "内置音色不可修改，或音色不存在";
}
QString SoundPlan::removeVoice(QString id){
    const auto path=m_outputRoot+"/voices/catalog.json";auto catalog=read(path);auto voices=catalog["voices"].toArray();
    for(int i=0;i<voices.size();++i){if(voices[i].toObject()["id"]!=id)continue;
        auto archived=catalog["archived"].toArray();archived.append(voices.takeAt(i));catalog["voices"]=voices;catalog["archived"]=archived;
        if(!saveVoiceCatalog(path,catalog))return "无法保存音色库";
        if(m_settings["voiceId"]==id){m_settings["voiceId"]="";m_settings["speechResult"]="";}
        loadVoices();emit edited();emit settingEdited("voiceId");return {};
    }return "内置音色不可移除，或音色不存在";
}
QString SoundPlan::restoreVoice(){
    const auto path=m_outputRoot+"/voices/catalog.json";auto catalog=read(path);auto archived=catalog["archived"].toArray();
    if(archived.isEmpty())return {};
    auto voices=catalog["voices"].toArray();voices.append(archived.takeAt(archived.size()-1));catalog["voices"]=voices;catalog["archived"]=archived;
    if(!saveVoiceCatalog(path,catalog))return "无法恢复音色";
    loadVoices();return {};
}
