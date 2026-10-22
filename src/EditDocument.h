#pragma once
#include <QObject>
#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QUrl>
#include <QJsonObject>
#include <QtQml/qqmlregistration.h>

class EditDocument : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    Q_PROPERTY(bool hasMask READ hasMask NOTIFY changed)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY changed)
    Q_PROPERTY(int imageWidth READ imageWidth NOTIFY changed)
    Q_PROPERTY(int imageHeight READ imageHeight NOTIFY changed)
    Q_PROPERTY(QString sourcePath READ sourcePath NOTIFY changed)
    Q_PROPERTY(QString resultUrl READ resultUrl NOTIFY changed)
    Q_PROPERTY(QString prompt MEMBER m_prompt NOTIFY changed)
    Q_PROPERTY(double strength MEMBER m_strength NOTIFY changed)
    Q_PROPERTY(int feather MEMBER m_feather NOTIFY changed)
    Q_PROPERTY(QString blendMode MEMBER m_blendMode NOTIFY changed)
    Q_PROPERTY(QString scope MEMBER m_scope NOTIFY changed)
public:
    explicit EditDocument(QObject *parent=nullptr):QObject(parent){}
    bool ready()const{return !m_source.isNull();}
    bool hasMask()const{return m_hasMask;}
    bool canUndo()const{return !m_undo.isEmpty();}
    bool canRedo()const{return !m_redo.isEmpty();}
    int imageWidth()const{return m_source.width();}
    int imageHeight()const{return m_source.height();}
    QString sourcePath()const{return m_sourcePath;}
    QString resultUrl()const{return m_resultUrl;}
    const QImage &source()const{return m_source;}
    const QImage &mask()const{return m_mask;}
    const QImage &result()const{return m_result;}
    QString load(QString path);
    bool restoreMask(QString path);
    void setResult(QString path);
    QJsonObject snapshot() const;
    QString restoreSnapshot(const QJsonObject &snapshot);
    void beginStroke();
    void paintLine(QPointF from,QPointF to,double diameter,bool erase);
    void paintRectangle(QRectF rectangle);
    void endStroke();
    Q_INVOKABLE void clearMask();
    Q_INVOKABLE void invertMask();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void addRectangle(double x,double y,double width,double height);
    bool prepare(QString directory,QString *error)const;
    static QImage blendWeights(const QImage &mask,int feather);
    static QImage composite(const QImage &source,const QImage &generated,const QImage &weights);
    static QImage finishImage(const QImage &source,const QImage &generated,const QImage &weights,const QString &mode);
    QString m_prompt;
    double m_strength=.85;
    int m_feather=12;
    QString m_blendMode="feather";
    QString m_scope="local";
signals:
    void changed();
private:
    void remember();
    void recount();
    QImage m_source,m_mask,m_result;
    QString m_sourcePath,m_resultUrl;
    QList<QImage> m_undo,m_redo;
    bool m_hasMask=false;
    mutable qint64 m_maskCacheKey=0;
    mutable QString m_encodedMask;
};
