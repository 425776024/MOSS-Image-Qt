#pragma once
#include <QObject>
#include <QVariantList>
#include <QJsonObject>

class Document : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList regions READ regions NOTIFY changed)
    Q_PROPERTY(QString prompt READ prompt WRITE setPrompt NOTIFY changed)
    Q_PROPERTY(QString negativePrompt READ negativePrompt WRITE setNegativePrompt NOTIFY changed)
    Q_PROPERTY(QString style READ style WRITE setStyle NOTIFY changed)
    Q_PROPERTY(QString layoutMeta READ layoutMeta WRITE setLayoutMeta NOTIFY changed)
    Q_PROPERTY(QString validationMessage READ validationError NOTIFY changed)
    Q_PROPERTY(QString inputPreview READ compiledPrompt NOTIFY changed)
    Q_PROPERTY(int canvasWidth READ canvasWidth WRITE setCanvasWidth NOTIFY changed)
    Q_PROPERTY(int canvasHeight READ canvasHeight WRITE setCanvasHeight NOTIFY changed)
    Q_PROPERTY(int selected READ selected WRITE setSelected NOTIFY selectionChanged)
    Q_PROPERTY(bool layout READ layout WRITE setLayout NOTIFY changed)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY changed)
public:
    explicit Document(QObject *p=nullptr);
    QVariantList regions() const { return m_regions; }
    QString prompt() const { return m_prompt; }
    QString negativePrompt() const { return m_negative; }
    QString style() const { return m_style; }
    QString layoutMeta() const { return m_meta.join(", "); }
    int canvasWidth() const { return m_width; }
    int canvasHeight() const { return m_height; }
    int selected() const { return m_selected; }
    bool layout() const { return m_layout; }
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    void setPrompt(QString value);
    void setNegativePrompt(QString value);
    void setStyle(QString value);
    void setLayoutMeta(QString value);
    void setCanvasWidth(int value);
    void setCanvasHeight(int value);
    void setSelected(int value);
    void setLayout(bool value);
    Q_INVOKABLE void addRegion(double x, double y, double width, double height, QString type="object");
    Q_INVOKABLE void updateRegion(int index, QString field, QVariant value);
    Q_INVOKABLE void moveRegion(int index, double x, double y, double width, double height);
    Q_INVOKABLE void removeSelected();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void example();
    Q_INVOKABLE void setCanvasSize(int width, int height);
    Q_INVOKABLE void duplicateSelected();
    Q_INVOKABLE void loadTemplate(QString name);
    Q_INVOKABLE QString importLayout(QString source, int width, int height);
    Q_INVOKABLE QString compiledPrompt() const;
    QString validationError() const;
    QJsonObject json() const;
    bool load(const QJsonObject &object, QString *error);
    bool switchPage(const QJsonObject &object, QString *error);
signals:
    void changed();
    void selectionChanged();
private:
    void remember();
    void restore(const QJsonObject &object);
    QVariantList m_regions;
    QString m_prompt;
    QString m_negative, m_style;
    QStringList m_meta;
    int m_width=1024, m_height=1024, m_selected=-1;
    bool m_layout=false;
    QList<QJsonObject> m_undo, m_redo;
};
