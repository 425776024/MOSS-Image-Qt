#include "EditDocument.h"
#include <QImageReader>
#include <QPainter>
#include <QDir>
#include <QBuffer>
#include <algorithm>
#include <cmath>

QString EditDocument::load(QString path){
    QImageReader reader(path);reader.setAutoTransform(true);
    auto size=reader.size();
    if(!size.isValid()||size.width()>2048||size.height()>2048)
        return "请选择宽、高均不超过 2048 像素的图片";
    auto image=reader.read();if(image.isNull())return "无法读取图片："+reader.errorString();
    m_source=image.convertToFormat(QImage::Format_ARGB32);
    m_mask=QImage(m_source.size(),QImage::Format_ARGB32);m_mask.fill(Qt::black);
    m_sourcePath=path;m_result={};m_resultUrl.clear();m_undo.clear();m_redo.clear();m_hasMask=false;
    emit changed();return {};
}
bool EditDocument::restoreMask(QString path){
    QImage image(path);if(image.size()!=m_source.size())return false;
    m_mask=image.convertToFormat(QImage::Format_ARGB32);m_undo.clear();m_redo.clear();recount();emit changed();return true;
}
void EditDocument::setResult(QString path){
    m_result=QImage(path);m_resultUrl=m_result.isNull()?QString():QUrl::fromLocalFile(path).toString();emit changed();
}
QJsonObject EditDocument::snapshot()const{
    // Cache the PNG across prompt edits and progress notifications.
    if(m_mask.cacheKey()!=m_maskCacheKey){
        QByteArray bytes;QBuffer buffer(&bytes);buffer.open(QIODevice::WriteOnly);
        if(!m_mask.isNull())m_mask.save(&buffer,"PNG");
        m_encodedMask=QString::fromLatin1(bytes.toBase64());m_maskCacheKey=m_mask.cacheKey();
    }
    return {{"source",m_sourcePath},{"mask",m_encodedMask},{"result",m_resultUrl},
        {"prompt",m_prompt},{"strength",m_strength},{"feather",m_feather},{"blendMode",m_blendMode}};
}
QString EditDocument::restoreSnapshot(const QJsonObject &state){
    // A page with no edit must also clear the previous page's source and undo.
    m_source={};m_mask={};m_result={};m_sourcePath.clear();m_resultUrl.clear();
    m_undo.clear();m_redo.clear();m_hasMask=false;m_encodedMask.clear();m_maskCacheKey=0;
    m_prompt=state["prompt"].toString();
    m_strength=std::clamp(state["strength"].toDouble(.85),.1,1.);
    m_feather=std::clamp(state["feather"].toInt(12),0,64);
    m_blendMode=state["blendMode"].toString()=="direct"?"direct":"feather";
    QString error;
    if(!state["source"].toString().isEmpty()){
        error=load(state["source"].toString());
        if(error.isEmpty()){
            const auto bytes=QByteArray::fromBase64(state["mask"].toString().toLatin1());
            QBuffer buffer;buffer.setData(bytes);buffer.open(QIODevice::ReadOnly);
            QImageReader reader(&buffer,"PNG");
            if(reader.size()==m_source.size()){
                const auto mask=reader.read();
                if(!mask.isNull())m_mask=mask.convertToFormat(QImage::Format_ARGB32);
            }
            recount();
            const auto result=QUrl(state["result"].toString()).toLocalFile();
            if(!result.isEmpty())setResult(result);
        }
    }
    emit changed();return error;
}
void EditDocument::remember(){
    if(!ready())return;
    m_undo.append(m_mask);m_redo.clear();
    const int count=std::max(1,std::min(16,int(64LL*1024*1024/std::max<qint64>(1,m_mask.sizeInBytes()))));
    while(m_undo.size()>count)m_undo.removeFirst();
}
void EditDocument::recount(){
    m_hasMask=false;
    for(int y=0;y<m_mask.height()&&!m_hasMask;++y){
        auto row=reinterpret_cast<const QRgb*>(m_mask.constScanLine(y));
        for(int x=0;x<m_mask.width();++x)if(qRed(row[x])>127){m_hasMask=true;break;}
    }
}
void EditDocument::beginStroke(){remember();}
void EditDocument::paintLine(QPointF a,QPointF b,double diameter,bool erase){
    if(!ready()||!std::isfinite(diameter)||diameter<=0)return;
    QPainter p(&m_mask);p.setRenderHint(QPainter::Antialiasing,false);
    const QColor color=erase?Qt::black:Qt::white;
    p.setPen(QPen(color,std::clamp(diameter,1.,512.),Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
    if(a==b){p.setPen(Qt::NoPen);p.setBrush(color);p.drawEllipse(a,diameter/2,diameter/2);}else p.drawLine(a,b);
    p.end();if(!erase)m_hasMask=true;emit changed();
}
void EditDocument::paintRectangle(QRectF r){
    if(!ready())return;
    QPainter p(&m_mask);p.fillRect(r.normalized().intersected(QRectF(m_source.rect())),Qt::white);p.end();
    recount();emit changed();
}
void EditDocument::endStroke(){
    if(!m_undo.isEmpty()&&m_mask==m_undo.last())m_undo.removeLast();recount();emit changed();
}
void EditDocument::addRectangle(double x,double y,double w,double h){
    if(!std::isfinite(x+y+w+h)||!ready())return;beginStroke();paintRectangle({x,y,w,h});endStroke();
}
void EditDocument::clearMask(){if(!hasMask())return;remember();m_mask.fill(Qt::black);recount();emit changed();}
void EditDocument::invertMask(){
    if(!ready())return;remember();m_mask.invertPixels(QImage::InvertRgb);recount();emit changed();
}
void EditDocument::undo(){if(m_undo.isEmpty())return;m_redo.append(m_mask);m_mask=m_undo.takeLast();recount();emit changed();}
void EditDocument::redo(){if(m_redo.isEmpty())return;m_undo.append(m_mask);m_mask=m_redo.takeLast();recount();emit changed();}
QImage EditDocument::blendWeights(const QImage &mask,int feather){
    const auto rgb=mask.convertToFormat(QImage::Format_ARGB32);const int w=rgb.width(),h=rgb.height();
    QImage weights(w,h,QImage::Format_Grayscale8);weights.fill(0);
    if(w==0||h==0)return weights;
    feather=std::clamp(feather,0,128);
    // Inward feather: pixels outside the user's selection always have zero weight.
    QVector<int> distance(w*h,0);const int cap=std::max(1,feather);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){
        if(qRed(rgb.pixel(x,y))<128)continue;
        int value=cap;
        if(x>0)value=std::min(value,distance[y*w+x-1]+1);
        if(y>0)value=std::min(value,distance[(y-1)*w+x]+1);
        distance[y*w+x]=value;
    }
    for(int y=h-1;y>=0;--y)for(int x=w-1;x>=0;--x){
        int &value=distance[y*w+x];if(!value)continue;
        if(x+1<w)value=std::min(value,distance[y*w+x+1]+1);
        if(y+1<h)value=std::min(value,distance[(y+1)*w+x]+1);
        weights.scanLine(y)[x]=uchar(std::clamp(value*255/cap,0,255));
    }
    return weights;
}
QImage EditDocument::composite(const QImage &source,const QImage &generated,const QImage &weights){
    if(source.isNull()||weights.size()!=source.size()||generated.width()<source.width()||generated.height()<source.height())return {};
    QImage result=source.convertToFormat(QImage::Format_ARGB32);
    const auto newImage=generated.convertToFormat(QImage::Format_ARGB32);
    const auto alpha=weights.convertToFormat(QImage::Format_Grayscale8);
    for(int y=0;y<result.height();++y){
        auto dst=reinterpret_cast<QRgb*>(result.scanLine(y));auto src=reinterpret_cast<const QRgb*>(newImage.constScanLine(y));
        const uchar *a=alpha.constScanLine(y);
        for(int x=0;x<result.width();++x){
            if(!a[x])continue;
            const auto old=dst[x],fresh=src[x];const int v=a[x];
            const auto mix=[v](int p,int q){return (p*(255-v)+q*v+127)/255;};
            dst[x]=qRgba(mix(qRed(old),qRed(fresh)),mix(qGreen(old),qGreen(fresh)),mix(qBlue(old),qBlue(fresh)),qAlpha(old));
        }
    }
    return result;
}
QImage EditDocument::finishImage(const QImage &source,const QImage &generated,const QImage &weights,const QString &mode){
    if(mode!="direct")return composite(source,generated,weights);
    if(source.isNull()||generated.width()<source.width()||generated.height()<source.height())return {};
    // Remove inference padding without pasting any source pixels back into the decoded image.
    return generated.copy(QRect(QPoint(0,0),source.size()));
}
bool EditDocument::prepare(QString dir,QString *error)const{
    if(!ready()||!hasMask()){if(error)*error="请先导入原图并画出需要修改的选区";return false;}
    const QSize padded(std::max(256,(m_source.width()+15)/16*16),std::max(256,(m_source.height()+15)/16*16));
    QImage init(padded,QImage::Format_RGB32);init.fill(Qt::white);
    {QPainter p(&init);p.drawImage(0,0,m_source);}
    QImage mask(padded,QImage::Format_RGB32);mask.fill(Qt::black);
    {QPainter p(&mask);p.drawImage(0,0,m_mask);}
    const bool ok=m_source.save(dir+"/source.png")&&m_mask.save(dir+"/selection.png")&&
        init.save(dir+"/init.png")&&mask.save(dir+"/mask.png")&&blendWeights(m_mask,m_feather).save(dir+"/blend.png");
    if(!ok&&error)*error="无法保存局部重绘的原图或遮罩";return ok;
}
