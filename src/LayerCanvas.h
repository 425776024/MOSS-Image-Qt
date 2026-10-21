#pragma once
#include "LayerDocument.h"
#include <QQuickPaintedItem>

class LayerCanvas : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(LayerDocument* document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(bool keepAspect MEMBER m_keepAspect NOTIFY controlsChanged)
    Q_PROPERTY(bool overlayMode MEMBER m_overlayMode NOTIFY controlsChanged)
public:
    explicit LayerCanvas(QQuickItem *parent=nullptr);
    LayerDocument *document()const{return m_document;}
    void setDocument(LayerDocument *document);
    void paint(QPainter *p)override;
signals:
    void documentChanged();
    void controlsChanged();
    void canvasClicked();
protected:
    void mousePressEvent(QMouseEvent *e)override;
    void mouseMoveEvent(QMouseEvent *e)override;
    void mouseReleaseEvent(QMouseEvent *e)override;
private:
    QRectF canvasRect()const;
    QPointF point(QPointF position)const;
    LayerDocument *m_document=nullptr;
    LayerDocument::Layer m_original;
    QPointF m_start;
    bool m_dragging=false,m_resizing=false,m_keepAspect=true,m_overlayMode=false;
};
