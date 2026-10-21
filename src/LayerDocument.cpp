#include "LayerDocument.h"
#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QUuid>
#include <cmath>

namespace {
QString newId(){return QUuid::createUuid().toString(QUuid::WithoutBraces);}
bool validSize(int w,int h){return w>=256&&h>=256&&w<=2048&&h<=2048;}
bool finiteRect(QRectF r){
    return std::isfinite(r.x())&&std::isfinite(r.y())&&std::isfinite(r.width())&&std::isfinite(r.height())&&
        std::abs(r.x())<=32768&&std::abs(r.y())<=32768&&r.width()>=1&&r.height()>=1&&r.width()<=16384&&r.height()<=16384;
}
}
bool LayerDocument::report(QString text,bool success){m_message=text;emit messageChanged();return success;}
bool LayerDocument::hasContent()const{
    for(const auto &l:m_state.layers)if(l.visible&&l.opacity>0)return true;return false;
}
void LayerDocument::setSelected(int i){i=validIndex(i)?i:-1;if(i!=m_selected){m_selected=i;emit changed();}}
QVariantList LayerDocument::layers()const{
    QVariantList result;
    for(const auto &l:m_state.layers)result.append(QVariantMap{{"id",l.id},{"name",l.name},{"kind",l.asset->kind},
        {"thumbnail",l.asset->thumbnail},{"x",l.rect.x()},{"y",l.rect.y()},{"width",l.rect.width()},
        {"height",l.rect.height()},{"rotation",l.rotation},{"opacity",l.opacity},{"visible",l.visible}});
    return result;
}
QSharedPointer<LayerDocument::Asset> LayerDocument::decode(QByteArray bytes,QString kind,QString *error){
    if(bytes.isEmpty()||bytes.size()>32*1024*1024){*error="单个素材需在 32 MB 以内";return {};}
    auto a=QSharedPointer<Asset>::create();a->bytes=bytes;a->kind=kind;
    QImage thumb;
    if(kind=="svg"){
        if(bytes.size()>8*1024*1024){*error="SVG 需在 8 MB 以内";return {};}
        a->svg=QSharedPointer<QSvgRenderer>::create(bytes);
        if(!a->svg->isValid()||a->svg->animated()){*error="请选择有效的静态 SVG";return {};}
        a->size=a->svg->defaultSize();
        if(a->size.isEmpty())a->size=a->svg->viewBox().size();
        if(a->size.isEmpty()){*error="SVG 缺少有效尺寸或 viewBox";return {};}
        thumb=QImage(a->size.scaled(72,72,Qt::KeepAspectRatio),QImage::Format_ARGB32_Premultiplied);
        thumb.fill(Qt::transparent);QPainter p(&thumb);a->svg->render(&p,QRectF(thumb.rect()));
    }else{
        QBuffer buffer(&a->bytes);buffer.open(QIODevice::ReadOnly);QImageReader reader(&buffer);reader.setAutoTransform(true);
        const auto format=reader.format().toLower();
        if(format!="png"&&format!="jpeg"&&format!="jpg"&&format!="webp"){
            *error="支持 PNG、JPEG、WebP 和 SVG";return {};
        }
        const auto size=reader.size();
        if(!size.isValid()||qint64(size.width())*size.height()>32000000){*error="单张位图不能超过 3200 万像素";return {};}
        a->image=reader.read().convertToFormat(QImage::Format_ARGB32_Premultiplied);
        if(a->image.isNull()){*error="图片解码失败："+reader.errorString();return {};}
        a->size=a->image.size();a->kind="image";
        thumb=a->image.scaled(72,72,Qt::KeepAspectRatio,Qt::SmoothTransformation);
    }
    QByteArray png;QBuffer b(&png);b.open(QIODevice::WriteOnly);thumb.save(&b,"PNG");
    a->thumbnail="data:image/png;base64,"+QString::fromLatin1(png.toBase64());return a;
}
bool LayerDocument::importFiles(const QList<QUrl> &urls){
    if(urls.isEmpty())return false;
    if(m_state.layers.size()+urls.size()>32)return report("最多支持 32 个图层");
    QList<Layer> added;
    qint64 pixels=0,bytes=0;
    QSet<const Asset*> seen;
    for(const auto &l:m_state.layers)if(!seen.contains(l.asset.data())){
        seen.insert(l.asset.data());pixels+=l.asset->image.width()*qint64(l.asset->image.height());bytes+=l.asset->bytes.size();
    }
    for(const auto &url:urls){
        if(!url.isLocalFile())return report("请导入本地素材");
        QFile f(url.toLocalFile());
        if(!f.open(QIODevice::ReadOnly)||f.size()>32*1024*1024)return report("无法读取素材，或文件超过 32 MB");
        QString error;auto asset=decode(f.readAll(),QFileInfo(f.fileName()).suffix().toLower()=="svg"?"svg":"image",&error);
        if(!asset)return report(error);
        pixels+=asset->image.width()*qint64(asset->image.height());bytes+=asset->bytes.size();
        if(pixels>64000000||bytes>96*1024*1024)return report("当前工程素材过多，请先缩小位图尺寸");
        QSizeF size=asset->size;size.scale(m_state.size,Qt::KeepAspectRatio);
        added.append({asset,newId(),QFileInfo(f.fileName()).completeBaseName(),
            QRectF((canvasWidth()-size.width())/2,(canvasHeight()-size.height())/2,size.width(),size.height())});
    }
    remember();m_state.layers.append(added);m_selected=m_state.layers.size()-1;emit changed();
    return report(QString("已导入 %1 个图层 · 原始素材已保留").arg(added.size()),true);
}
QTransform LayerDocument::transform(const Layer &l){
    QTransform t;t.translate(l.rect.center().x(),l.rect.center().y());t.rotate(l.rotation);
    t.translate(-l.rect.width()/2,-l.rect.height()/2);return t;
}
void LayerDocument::paint(QPainter *p,bool includeBackground)const{
    p->save();p->setClipRect(QRect(QPoint(),m_state.size));
    if(includeBackground&&m_state.background!="transparent")p->fillRect(QRect(QPoint(),m_state.size),QColor(m_state.background));
    p->setRenderHint(QPainter::Antialiasing);p->setRenderHint(QPainter::SmoothPixmapTransform);
    for(const auto &l:m_state.layers){
        if(!l.visible||l.opacity<=0)continue;
        // A layer's opacity applies to the complete SVG, not separately to
        // each overlapping SVG primitive.
        if(l.asset->svg&&l.opacity<1){
            QImage surface(m_state.size,QImage::Format_ARGB32_Premultiplied);surface.fill(Qt::transparent);
            {QPainter svgPainter(&surface);svgPainter.setRenderHint(QPainter::Antialiasing);
                svgPainter.setTransform(transform(l));
                l.asset->svg->render(&svgPainter,QRectF(0,0,l.rect.width(),l.rect.height()));}
            p->save();p->setOpacity(l.opacity);p->drawImage(QPoint(),surface);p->restore();continue;
        }
        p->save();p->setOpacity(l.opacity);p->setTransform(transform(l),true);
        const QRectF dest(0,0,l.rect.width(),l.rect.height());
        if(l.asset->svg)l.asset->svg->render(p,dest);else p->drawImage(dest,l.asset->image);
        p->restore();
    }
    p->restore();
}
QImage LayerDocument::composite()const{
    QImage image(m_state.size,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);
    QPainter p(&image);paint(&p);return image;
}
void LayerDocument::remember(){
    m_undo.append(m_state);m_redo.clear();
    // Geometry-only snapshots share assets; removed large photos must not
    // accumulate unbounded decoded image memory in the undo history.
    auto retainedBytes=[this]{
        QSet<const Asset*> assets;qint64 bytes=0;
        for(const auto &state:m_undo)for(const auto &l:state.layers)
            if(!assets.contains(l.asset.data())){assets.insert(l.asset.data());bytes+=l.asset->bytes.size()+l.asset->image.sizeInBytes();}
        return bytes;
    };
    while(m_undo.size()>1&&(m_undo.size()>30||retainedBytes()>512LL*1024*1024))m_undo.removeFirst();
    emit contentChanged();
}
void LayerDocument::setCanvasSize(int w,int h){
    if(!validSize(w,h)){report("画布宽高需在 256–2048 像素之间");return;}
    if(m_state.size==QSize(w,h))return;remember();m_state.size={w,h};emit changed();
}
void LayerDocument::setBackground(QString color){
    if(color!="transparent"&&!QColor(color).isValid())return;
    if(color==m_state.background)return;remember();m_state.background=color;emit changed();
}
void LayerDocument::updateLayer(int i,QString key,QVariant value){
    if(!validIndex(i))return;auto l=m_state.layers[i];
    if(key=="name"){auto name=value.toString().trimmed();if(name.isEmpty())return;l.name=name.left(120);}
    else if(key=="visible")l.visible=value.toBool();
    else{
        bool ok=false;double n=value.toDouble(&ok);if(!ok||!std::isfinite(n))return;
        if(key=="opacity")l.opacity=std::clamp(n,0.,1.);
        else if(key=="rotation")l.rotation=std::clamp(n,-180.,180.);
        else if(key=="x")l.rect.moveLeft(n);
        else if(key=="y")l.rect.moveTop(n);
        else if(key=="width")l.rect.setWidth(n);
        else if(key=="height")l.rect.setHeight(n);
        else return;
        if(!finiteRect(l.rect))return;
    }
    remember();m_state.layers[i]=l;emit changed();
}
void LayerDocument::fitSelected(){
    if(!validIndex(m_selected))return;remember();auto &l=m_state.layers[m_selected];
    QSizeF size=l.asset->size;size.scale(m_state.size,Qt::KeepAspectRatio);
    l.rect=QRectF((canvasWidth()-size.width())/2,(canvasHeight()-size.height())/2,size.width(),size.height());
    l.rotation=0;emit changed();
}
void LayerDocument::centerSelected(){
    if(!validIndex(m_selected))return;remember();m_state.layers[m_selected].rect.moveCenter(QPointF(canvasWidth()/2.,canvasHeight()/2.));emit changed();
}
void LayerDocument::duplicateSelected(){
    if(!validIndex(m_selected)||m_state.layers.size()>=32)return;remember();auto l=m_state.layers[m_selected];
    l.id=newId();l.name+=" 副本";l.rect.translate(24,24);
    m_state.layers.insert(++m_selected,l);emit changed();
}
void LayerDocument::removeSelected(){
    if(!validIndex(m_selected))return;remember();m_state.layers.removeAt(m_selected);
    m_selected=std::min(m_selected,int(m_state.layers.size())-1);emit changed();
}
void LayerDocument::moveSelected(int d){
    if(!validIndex(m_selected)||!validIndex(m_selected+d))return;remember();
    m_state.layers.move(m_selected,m_selected+d);m_selected+=d;emit changed();
}
void LayerDocument::undo(){
    if(m_undo.isEmpty())return;m_redo.append(m_state);m_state=m_undo.takeLast();m_selected=-1;emit changed();emit contentChanged();
}
void LayerDocument::redo(){
    if(m_redo.isEmpty())return;m_undo.append(m_state);m_state=m_redo.takeLast();m_selected=-1;emit changed();emit contentChanged();
}
void LayerDocument::beginTransform(){if(validIndex(m_selected)&&!m_transforming){remember();m_transforming=true;}}
void LayerDocument::transformSelected(QRectF rect){
    if(!m_transforming||!validIndex(m_selected)||!finiteRect(rect))return;
    m_state.layers[m_selected].rect=rect;emit changed();emit contentChanged();
}
void LayerDocument::endTransform(){
    if(m_transforming&&!m_undo.isEmpty()&&validIndex(m_selected)&&
       m_state.layers[m_selected].rect==m_undo.last().layers[m_selected].rect)m_undo.removeLast();
    m_transforming=false;emit changed();
}
QJsonObject LayerDocument::json()const{
    QJsonArray layers,assets;QList<const Asset*> seen;
    for(const auto &l:m_state.layers){
        int asset=seen.indexOf(l.asset.data());
        if(asset<0){asset=seen.size();seen.append(l.asset.data());
            assets.append(QJsonObject{{"kind",l.asset->kind},{"data",QString::fromLatin1(l.asset->bytes.toBase64())}});}
        layers.append(QJsonObject{{"id",l.id},{"name",l.name},{"asset",asset},{"x",l.rect.x()},{"y",l.rect.y()},
            {"width",l.rect.width()},{"height",l.rect.height()},{"rotation",l.rotation},{"opacity",l.opacity},{"visible",l.visible}});
    }
    return {{"format","moss-layers"},{"version",1},{"width",canvasWidth()},{"height",canvasHeight()},
        {"background",m_state.background},{"assets",assets},{"layers",layers}};
}
bool LayerDocument::load(const QJsonObject &o){
    State next;next.size={o["width"].toInt(),o["height"].toInt()};next.background=o["background"].toString("transparent");
    if(o["format"]!="moss-layers"||o["version"].toInt()!=1||!validSize(next.size.width(),next.size.height())||
       !o["layers"].isArray()||o["layers"].toArray().size()>32||!o["assets"].isArray()||o["assets"].toArray().size()>32||
       (next.background!="transparent"&&!QColor(next.background).isValid()))return report("图层工程格式或画布尺寸无效");
    qint64 pixels=0,bytes=0;QList<QSharedPointer<Asset>> assets;
    for(const auto &v:o["assets"].toArray()){
        auto j=v.toObject();QString error;auto a=decode(QByteArray::fromBase64(j["data"].toString().toLatin1()),j["kind"].toString(),&error);
        if(!a)return report(error);
        pixels+=a->image.width()*qint64(a->image.height());bytes+=a->bytes.size();
        if(pixels>64000000||bytes>96*1024*1024)return report("工程素材超过容量限制");
        assets.append(a);
    }
    QSet<QString> layerIds;
    for(const auto &v:o["layers"].toArray()){
        auto j=v.toObject();int index=j["asset"].toInt(-1);
        if(index<0||index>=assets.size())return report("图层素材引用无效");
        auto a=assets[index];
        auto id=j["id"].toString();
        if(id.isEmpty()||layerIds.contains(id))id=newId();
        layerIds.insert(id);
        Layer l{a,id,j["name"].toString(),{j["x"].toDouble(),j["y"].toDouble(),j["width"].toDouble(),j["height"].toDouble()},
            j["opacity"].toDouble(1),j["rotation"].toDouble(),j["visible"].toBool(true)};
        if(!finiteRect(l.rect)||!std::isfinite(l.opacity)||l.opacity<0||l.opacity>1||
           !std::isfinite(l.rotation)||std::abs(l.rotation)>180)return report("图层参数无效");
        next.layers.append(l);
    }
    remember();m_state=next;m_selected=m_state.layers.isEmpty()?-1:m_state.layers.size()-1;emit changed();
    return report("图层工程已恢复，素材包含在工程内",true);
}
void LayerDocument::clear(){
    m_state=State{};m_selected=-1;m_transforming=false;m_undo.clear();m_redo.clear();m_message.clear();
    emit changed();emit contentChanged();emit messageChanged();
}
bool LayerDocument::restoreSnapshot(const QJsonObject &object){
    if(object.isEmpty()){clear();return true;}
    if(!load(object))return false;
    m_selected=-1;m_transforming=false;m_undo.clear();m_redo.clear();
    emit changed();return true;
}
bool LayerDocument::saveProject(QUrl url){
    auto data=QJsonDocument(json()).toJson(QJsonDocument::Compact);QSaveFile f(url.toLocalFile());
    if(!f.open(QIODevice::WriteOnly)||f.write(data)!=data.size()||!f.commit())return report("图层工程保存失败");
    return report("图层工程已保存（包含原始素材）",true);
}
bool LayerDocument::loadProject(QUrl url){
    QFile f(url.toLocalFile());if(!f.open(QIODevice::ReadOnly)||f.size()>140*1024*1024)return report("无法读取图层工程，或文件超过 140 MB");
    QJsonParseError e;const auto o=QJsonDocument::fromJson(f.readAll(),&e);
    if(e.error!=QJsonParseError::NoError||!o.isObject())return report("图层工程 JSON 格式错误");
    return load(o.object());
}
bool LayerDocument::exportPng(QUrl url){
    QSaveFile f(url.toLocalFile());
    if(!f.open(QIODevice::WriteOnly)||!composite().save(&f,"PNG")||!f.commit())return report("合成图片保存失败");
    return report("合成 PNG 已保存",true);
}
