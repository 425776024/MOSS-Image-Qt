#pragma once
#include <QObject>
#include <QImage>
#include <QJsonObject>
#include <QPainter>
#include <QSharedPointer>
#include <QSvgRenderer>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class LayerDocument : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(QVariantList layers READ layers NOTIFY changed)
    Q_PROPERTY(int selected READ selected WRITE setSelected NOTIFY changed)
    Q_PROPERTY(int canvasWidth READ canvasWidth NOTIFY changed)
    Q_PROPERTY(int canvasHeight READ canvasHeight NOTIFY changed)
    Q_PROPERTY(QString background READ background NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY changed)
    Q_PROPERTY(bool hasContent READ hasContent NOTIFY changed)
public:
    struct Asset {
        QByteArray bytes;
        QImage image;
        QSharedPointer<QSvgRenderer> svg;
        QSize size;
        QString kind, thumbnail;
    };
    struct Layer {
        QSharedPointer<Asset> asset;
        QString id, name;
        QRectF rect;
        double opacity=1, rotation=0;
        bool visible=true;
    };
    explicit LayerDocument(QObject *parent=nullptr):QObject(parent){}
    QVariantList layers() const;
    int selected() const{return m_selected;}
    void setSelected(int index);
    int canvasWidth() const{return m_state.size.width();}
    int canvasHeight() const{return m_state.size.height();}
    QString background() const{return m_state.background;}
    QString message() const{return m_message;}
    bool canUndo() const{return !m_undo.isEmpty();}
    bool canRedo() const{return !m_redo.isEmpty();}
    bool hasContent() const;
    const QList<Layer>& entries() const{return m_state.layers;}
    static QTransform transform(const Layer &layer);
    void paint(QPainter *p,bool includeBackground=true) const;
    QImage composite() const;
    QJsonObject json() const;
    bool load(const QJsonObject &object);
    void clear();
    bool restoreSnapshot(const QJsonObject &object);
    Q_INVOKABLE bool importFiles(const QList<QUrl> &urls);
    Q_INVOKABLE bool saveProject(QUrl url);
    Q_INVOKABLE bool loadProject(QUrl url);
    Q_INVOKABLE bool exportPng(QUrl url);
    Q_INVOKABLE void setCanvasSize(int width,int height);
    Q_INVOKABLE void setBackground(QString color);
    Q_INVOKABLE void updateLayer(int index,QString key,QVariant value);
    Q_INVOKABLE void fitSelected();
    Q_INVOKABLE void centerSelected();
    Q_INVOKABLE void duplicateSelected();
    Q_INVOKABLE void removeSelected();
    Q_INVOKABLE void moveSelected(int delta);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    void beginTransform();
    void transformSelected(QRectF rect);
    void endTransform();
signals:
    void changed();
    void contentChanged();
    void messageChanged();
private:
    struct State {QList<Layer> layers;QSize size{1024,1024};QString background="transparent";};
    static QSharedPointer<Asset> decode(QByteArray bytes,QString kind,QString *error);
    void remember();
    bool report(QString text,bool success=false);
    bool validIndex(int i) const{return i>=0&&i<m_state.layers.size();}
    State m_state;
    QList<State> m_undo,m_redo;
    int m_selected=-1;
    QString m_message;
    bool m_transforming=false;
};
