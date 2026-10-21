#include "Studio.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QUuid>

namespace {
QString diskPath(QString value) { return value.startsWith("file:") ? QUrl(value).toLocalFile() : value; }
bool saveJson(QString path, const QJsonObject &value) {
    QSaveFile file(path);
    const auto bytes=QJsonDocument(value).toJson();
    return file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size()&&file.commit();
}
}

QImage Studio::canvasImage() const {
    if(!hasCanvasContent())return {};
    QImage result(1024,1024,QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);
    QPainter painter(&result);painter.setRenderHint(QPainter::SmoothPixmapTransform);
    if(!m_pageVisual.isEmpty()){
        QImageReader reader(diskPath(m_pageVisual));reader.setAutoTransform(true);
        const auto base=reader.read();if(base.isNull())return {};
        QSizeF fitted=base.size();fitted.scale(result.size(),Qt::KeepAspectRatio);
        painter.drawImage(QRectF((result.width()-fitted.width())/2,(result.height()-fitted.height())/2,
                                fitted.width(),fitted.height()),base);
    }
    painter.scale(1024./m_layers.canvasWidth(),1024./m_layers.canvasHeight());
    m_layers.paint(&painter,false);
    return result;
}
QString Studio::canvasCompositeUrl() const {
    if(!hasCanvasContent())return {};
    return QString("image://moss-canvas/%1/%2/%3").arg(m_currentPage).arg(m_canvasRevision).arg(qHash(m_pageVisual));
}

QString Studio::canvasSnapshot() {
    if(m_canvasSnapshotBase==m_pageVisual&&m_canvasSnapshotRevision==m_canvasRevision&&
       QFileInfo::exists(m_canvasSnapshot))return m_canvasSnapshot;
    const auto image=canvasImage();
    if(image.isNull()){fail("请先添加可读取的画布图像或对象");return {};}
    const auto directory=m_outputRoot+"/creation-assets";
    const auto path=directory+"/canvas-"+QUuid::createUuid().toString(QUuid::WithoutBraces)+".png";
    QSaveFile file(path);
    if(!QDir().mkpath(directory)||!file.open(QIODevice::WriteOnly)||!image.save(&file,"PNG")||!file.commit()){
        fail("无法保存画布合成快照");return {};
    }
    m_canvasSnapshot=path;m_canvasSnapshotBase=m_pageVisual;m_canvasSnapshotRevision=m_canvasRevision;
    return path;
}

void Studio::exportCanvas(QUrl destination) {
    if(busy()||destination.toLocalFile().isEmpty())return;
    m_error.clear();const auto snapshot=canvasSnapshot();
    if(!snapshot.isEmpty())exportFile(snapshot,destination);
}

void Studio::syncTimelineSources() {
    m_timeline.setSource("image",m_pageVisual,"image");
    const auto speech=effectiveSpeech();
    m_timeline.setSource("speech",speech.isEmpty()?QString():m_pageVideo.isEmpty()?speech:m_pageVideo,
                         !speech.isEmpty()&&!m_pageVideo.isEmpty()?"video":"audio");
    m_timeline.setValue("speech","audioSource",speech);
    const auto settings=m_sound.settings();
    m_timeline.setSource("sfx",settings["sfxResult"].toString(),"audio");
    if(!settings["sfxEnabled"].toBool())m_timeline.setValue("sfx","enabled",false);
    m_timeline.setValue("sfx","gain",settings["sfxGain"]);
}

void Studio::soundSettingEdited(QString key) {
    if(m_loadingPage)return;
    if(key=="voiceSource"||key=="voiceId"||key=="speechText"||key=="mouthReference"){
        m_pageVideo.clear();m_narrationRoute.clear();
    }
    syncTimelineSources();
    if(key=="sfxEnabled")m_timeline.setValue("sfx","enabled",m_sound.settings()["sfxEnabled"]);
    emit changed();
}

void Studio::generateTrack(QString key) {
    if(busy())return;
    m_error.clear();m_audioPipeline=false;m_trackGeneration=key;
    if(key=="image"){
        m_timeline.setValue(key,"enabled",true);
        if(m_repainting)generateEdit();else generate();
        return;
    }
    if(key=="voice"){m_trackGeneration="voice-only";key="speech";}
    if(key!="speech"&&key!="sfx")return;
    if(key=="speech"){
        if(m_sound.settings()["voiceSource"]=="none"){fail("选择合成语音或导入音频");return;}
    }else m_sound.set("sfxEnabled",true);
    if(!validateSound())return;
    if(m_trackGeneration=="voice-only"){
        m_pageVideo.clear();m_narrationRoute.clear();syncTimelineSources();
    }
    m_timeline.setValue(key,"enabled",true);
    m_audioPipeline=true;m_voiceDone=key=="sfx";m_sfxDone=key=="speech";
    // Generate the full new audio first; its measured length then defines the
    // editable range. The previous clip's trim must not truncate a new voice.
    m_pageVideoLimit=0;
    advanceAudioPipeline();
    if(!m_busy&&!m_audioPipeline)emit jobFinished(m_error.isEmpty());
}

void Studio::importTrackImage(QString key,QUrl url) {
    if(busy())return;
    if(key=="image"){importEditImage(url);setRepainting(false);m_timeline.select(key);return;}
    if(key!="background")return;
    QImageReader reader(url.toLocalFile());reader.setAutoTransform(true);
    const auto image=reader.read();
    if(image.isNull()){fail("请选择可读取的背景图片");return;}
    const auto dir=m_outputRoot+"/creation-assets";
    const auto path=dir+"/"+QUuid::createUuid().toString(QUuid::WithoutBraces)+".png";
    QSaveFile file(path);
    if(!QDir().mkpath(dir)||!file.open(QIODevice::WriteOnly)||!image.save(&file,"PNG")||!file.commit()){
        fail("无法保存背景图片");return;
    }
    m_timeline.setSource(key,QUrl::fromLocalFile(path).toString(),"image");
    m_timeline.setValue(key,"enabled",true);m_timeline.select(key);
    m_status="背景已加入轨道";emit changed();
}

void Studio::exportTrack(QString key,QUrl destination) {
    if(busy())return;
    const auto track=m_timeline.track(key);
    const auto source=diskPath(track["source"].toString());
    if(source.isEmpty()||!QFileInfo(source).isFile()){fail("此轨道还没有可导出的素材，请先生成或导入");return;}
    m_error.clear();exportFile(source,destination);
}

void Studio::exportTimeline(QUrl destination) {
    if(busy()||destination.toLocalFile().isEmpty())return;
    const auto worker=engine("moss-ditto");
    if(worker.isEmpty()){fail("未找到本地视频导出引擎");return;}
    QJsonArray tracks;
    const double end=m_timeline.duration();
    for(const auto &value:m_timeline.tracks()){
        auto row=value.toMap();
        if(!row["enabled"].toBool())continue;
        const bool color=row["kind"]=="color";
        if(row["source"].toString().isEmpty()&&!color)continue;
        if(!color&&!QFileInfo(diskPath(row["source"].toString())).isFile()){
            fail(row["name"].toString()+"的素材文件不可用，请重新生成或导入");return;
        }
        row["source"]=diskPath(row["source"].toString());
        if(row["audioSource"].toString().isEmpty()&&row["kind"]!="video")row.remove("audioSource");
        else row["audioSource"]=diskPath(row["audioSource"].toString());
        if(row["key"]=="background"){row["start"]=0.;row["duration"]=end;}
        tracks.append(QJsonObject::fromVariantMap(row));
    }
    if(hasCanvasContent()){
        const auto canvas=canvasSnapshot();if(canvas.isEmpty())return;
        tracks.append(QJsonObject{{"key","image"},{"kind","image"},{"source",canvas},{"enabled",true},
            {"start",0},{"duration",end},{"x",0},{"y",0},{"width",1},{"height",1}});
    }
    if(tracks.isEmpty()){fail("请先添加轨道内容");return;}
    m_jobDir=m_outputRoot+"/"+QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss")+"-timeline-"+QUuid::createUuid().toString(QUuid::Id128).left(6);
    if(!QDir().mkpath(m_jobDir)||
       !saveJson(m_jobDir+"/request.json",{{"directory",m_jobDir},{"width",1024},{"height",1024},{"fps",25},{"duration",end},{"tracks",tracks}})||
       !saveJson(m_jobDir+"/job.json",{{"kind","timeline"},{"started",QDateTime::currentDateTime().toString(Qt::ISODate)}})){
        fail("无法创建导出任务");return;
    }
    m_audioPipeline=false;m_timelineExport=destination;
    start(worker,{"render-timeline",m_jobDir+"/request.json"},"timeline");
}
