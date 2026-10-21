#include "MaskCanvas.h"
#include <QPainter>
#include <QMouseEvent>
#include <QHoverEvent>
MaskCanvas::MaskCanvas(QQuickItem *p):QQuickPaintedItem(p){
    setAcceptedMouseButtons(Qt::LeftButton);setAcceptHoverEvents(true);setClip(true);
    connect(this,&MaskCanvas::controlsChanged,this,[this]{update();});
}
void MaskCanvas::setDocument(EditDocument *d){
    if(m_document==d)return;if(m_document)disconnect(m_document,nullptr,this,nullptr);
    m_document=d;if(d)connect(d,&EditDocument::changed,this,[this]{update();});emit documentChanged();update();
}
QRectF MaskCanvas::imageRect()const{
    if(!m_document||!m_document->ready())return {};
    QSizeF size=m_document->source().size();size.scale(std::max(1.,width()-2*m_canvasPadding),std::max(1.,height()-2*m_canvasPadding),Qt::KeepAspectRatio);
    return {(width()-size.width())/2,(height()-size.height())/2,size.width(),size.height()};
}
QPointF MaskCanvas::imagePoint(QPointF point)const{
    const auto r=imageRect();if(r.isEmpty())return {};
    return {(point.x()-r.x())*m_document->imageWidth()/r.width(),(point.y()-r.y())*m_document->imageHeight()/r.height()};
}
void MaskCanvas::paint(QPainter *p){
    p->fillRect(boundingRect(),m_backgroundColor);if(!m_document||!m_document->ready())return;
    const auto r=imageRect();p->setRenderHint(QPainter::SmoothPixmapTransform);
    p->drawImage(r,m_showResult&&!m_document->result().isNull()?m_document->result():m_document->source());
    if(m_showMask&&!m_showResult){
        const auto &mask=m_document->mask();QImage overlay(mask.size(),QImage::Format_ARGB32);overlay.fill(Qt::transparent);
        for(int y=0;y<mask.height();++y){auto out=reinterpret_cast<QRgb*>(overlay.scanLine(y));auto row=reinterpret_cast<const QRgb*>(mask.constScanLine(y));
            for(int x=0;x<mask.width();++x)if(qRed(row[x])>127)out[x]=qRgba(185,211,156,120);}
        p->drawImage(r,overlay);
    }
    p->setPen(QPen(QColor("#BCC5D8"),1));p->setBrush(Qt::NoBrush);p->drawRect(r);
    if(m_drawing&&m_tool=="rectangle"){
        p->setPen(QPen(QColor("#B9D39C"),2,Qt::DashLine));p->setBrush(QColor(185,211,156,55));
        const double s=r.width()/m_document->imageWidth();
        p->drawRect(QRectF(r.topLeft()+m_start*s,r.topLeft()+m_current*s).normalized().intersected(r));
    }
    if(m_hovering&&!m_showResult&&m_tool!="rectangle"&&r.contains(m_hover)){
        const double radius=m_brushSize*r.width()/m_document->imageWidth()/2;
        p->setBrush(Qt::NoBrush);p->setPen(QPen(Qt::white,2));p->drawEllipse(m_hover,radius,radius);
        p->setPen(QPen(QColor("#B9D39C"),1));p->drawEllipse(m_hover,radius+1,radius+1);
    }
}
void MaskCanvas::mousePressEvent(QMouseEvent *e){
    if(!m_document||m_showResult||!imageRect().contains(e->position())){e->ignore();return;}
    forceActiveFocus();m_drawing=true;m_start=m_last=m_current=imagePoint(e->position());m_document->beginStroke();
    if(m_tool!="rectangle")m_document->paintLine(m_last,m_last,m_brushSize,m_tool=="erase");e->accept();update();
}
void MaskCanvas::mouseMoveEvent(QMouseEvent *e){
    m_hover=e->position();if(!m_drawing)return;m_current=imagePoint(e->position());
    if(m_tool!="rectangle")m_document->paintLine(m_last,m_current,m_brushSize,m_tool=="erase");m_last=m_current;update();
}
void MaskCanvas::mouseReleaseEvent(QMouseEvent *e){
    if(!m_drawing)return;m_current=imagePoint(e->position());
    if(m_tool=="rectangle")m_document->paintRectangle(QRectF(m_start,m_current));
    m_drawing=false;m_document->endStroke();e->accept();update();
}
void MaskCanvas::hoverMoveEvent(QHoverEvent *e){m_hovering=true;m_hover=e->position();update();}
void MaskCanvas::hoverLeaveEvent(QHoverEvent *){m_hovering=false;update();}
