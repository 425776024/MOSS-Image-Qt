#include "LayerCanvas.h"
#include <QMouseEvent>
#include <QLineF>

LayerCanvas::LayerCanvas(QQuickItem *p):QQuickPaintedItem(p){setAcceptedMouseButtons(Qt::LeftButton);setClip(true);}
void LayerCanvas::setDocument(LayerDocument *d){
    if(d==m_document)return;
    if(m_document)disconnect(m_document,nullptr,this,nullptr);m_document=d;
    if(d)connect(d,&LayerDocument::changed,this,[this]{update();});
    emit documentChanged();update();
}
QRectF LayerCanvas::canvasRect()const{
    if(!m_document)return {};
    if(m_overlayMode)return boundingRect();
    QSizeF size(m_document->canvasWidth(),m_document->canvasHeight());
    size.scale(std::max(1.,width()-64),std::max(1.,height()-64),Qt::KeepAspectRatio);
    return {(width()-size.width())/2,(height()-size.height())/2,size.width(),size.height()};
}
QPointF LayerCanvas::point(QPointF pos)const{
    const auto r=canvasRect();return (pos-r.topLeft())/(r.width()/m_document->canvasWidth());
}
void LayerCanvas::paint(QPainter *p){
    if(!m_overlayMode)p->fillRect(boundingRect(),QColor("#101113"));if(!m_document)return;
    const auto r=canvasRect();p->save();p->setClipRect(r);
    if(!m_overlayMode){
        p->fillRect(r,Qt::white);
        for(int y=0;y<r.height();y+=12)for(int x=0;x<r.width();x+=12)
            if((x/12+y/12)%2==0)p->fillRect(QRectF(r.left()+x,r.top()+y,12,12),QColor("#E8EBF0"));
    }
    p->translate(r.topLeft());const double s=r.width()/m_document->canvasWidth();p->scale(s,s);
    m_document->paint(p,!m_overlayMode);p->restore();
    if(!m_overlayMode){p->setPen(QPen(QColor("#303136"),1));p->setBrush(Qt::NoBrush);p->drawRect(r);}
    const auto i=m_document->selected();
    if(!isEnabled()||i<0||i>=m_document->entries().size())return;
    const auto &l=m_document->entries()[i];if(!l.visible)return;
    QTransform t;t.translate(r.x(),r.y());t.scale(s,s);
    const auto corners=t.map(LayerDocument::transform(l).map(QPolygonF(QRectF(0,0,l.rect.width(),l.rect.height()))));
    p->setRenderHint(QPainter::Antialiasing);p->setPen(QPen(QColor("#34D1BF"),1.5));
    p->drawPolygon(corners);
    const auto handle=corners[2];p->setBrush(Qt::white);p->drawRect(QRectF(handle.x()-5,handle.y()-5,10,10));
}
void LayerCanvas::mousePressEvent(QMouseEvent *e){
    if(!m_document)return;const auto pos=point(e->position());const auto &list=m_document->entries();
    int selected=m_document->selected();m_resizing=false;
    if(selected>=0&&selected<list.size()&&list[selected].visible){
        const auto &l=list[selected];auto corner=LayerDocument::transform(l).map(QPointF(l.rect.width(),l.rect.height()));
        m_resizing=QLineF(pos,corner).length()*canvasRect().width()/m_document->canvasWidth()<12;
    }
    if(!m_resizing){
        selected=-1;
        if(canvasRect().contains(e->position()))for(int i=list.size()-1;i>=0;--i){
            const auto &l=list[i];if(!l.visible||l.opacity<=0)continue;
            if(QRectF(0,0,l.rect.width(),l.rect.height()).contains(LayerDocument::transform(l).inverted().map(pos))){selected=i;break;}
        }
        m_document->setSelected(selected);
    }
    if(selected<0){emit canvasClicked();e->accept();return;}
    m_original=list[selected];m_start=pos;m_dragging=true;m_document->beginTransform();e->accept();
}
void LayerCanvas::mouseMoveEvent(QMouseEvent *e){
    if(!m_dragging||!m_document)return;
    const auto pos=point(e->position());auto rect=m_original.rect;
    if(m_resizing){
        const auto local=LayerDocument::transform(m_original).inverted().map(pos);
        double w=std::clamp(local.x(),1.,16384.),h=std::clamp(local.y(),1.,16384.);
        if(m_keepAspect){double scale=std::max(w/rect.width(),h/rect.height());scale=std::min(scale,16384./std::max(rect.width(),rect.height()));w=rect.width()*scale;h=rect.height()*scale;}
        // Keep the opposite corner stationary even when the layer is rotated.
        const auto anchor=LayerDocument::transform(m_original).map(QPointF());
        QTransform rotation;rotation.rotate(m_original.rotation);
        const QPointF half(w/2,h/2),origin=anchor-half+rotation.map(half);
        rect=QRectF(origin,QSizeF(w,h));
    }else rect.translate(pos-m_start);
    m_document->transformSelected(rect);e->accept();
}
void LayerCanvas::mouseReleaseEvent(QMouseEvent *e){
    if(!m_dragging)return;mouseMoveEvent(e);m_dragging=false;m_document->endTransform();e->accept();
}
