#pragma once
#include "EditDocument.h"
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>
class MaskCanvas:public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(EditDocument* document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(QColor backgroundColor MEMBER m_backgroundColor NOTIFY controlsChanged)
    Q_PROPERTY(QString tool MEMBER m_tool NOTIFY controlsChanged)
    Q_PROPERTY(double brushSize MEMBER m_brushSize NOTIFY controlsChanged)
    Q_PROPERTY(double canvasPadding MEMBER m_canvasPadding NOTIFY controlsChanged)
    Q_PROPERTY(bool showMask MEMBER m_showMask NOTIFY controlsChanged)
    Q_PROPERTY(bool showResult MEMBER m_showResult NOTIFY controlsChanged)
public:
    explicit MaskCanvas(QQuickItem *parent=nullptr);
    EditDocument* document()const{return m_document;}
    void setDocument(EditDocument *document);
    void paint(QPainter *painter)override;
protected:
    void mousePressEvent(QMouseEvent *event)override;
    void mouseMoveEvent(QMouseEvent *event)override;
    void mouseReleaseEvent(QMouseEvent *event)override;
    void hoverMoveEvent(QHoverEvent *event)override;
    void hoverLeaveEvent(QHoverEvent *event)override;
signals:
    void documentChanged();
    void controlsChanged();
private:
    QRectF imageRect()const;
    QPointF imagePoint(QPointF point)const;
    EditDocument *m_document=nullptr;
    QColor m_backgroundColor=QColor("#141819");
    QString m_tool="brush";
    double m_brushSize=48;
    double m_canvasPadding=24;
    bool m_showMask=true,m_showResult=false,m_drawing=false,m_hovering=false;
    QPointF m_start,m_last,m_current,m_hover;
};
