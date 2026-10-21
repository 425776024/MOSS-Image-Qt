#pragma once
#include <QObject>
#include <QVariantList>
#include <QUrl>
#include <QColor>

// Home-library files are owned by Moss, independently of the source material.
class WorkspaceLibrary : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList avatars READ avatars NOTIFY avatarsChanged)
    Q_PROPERTY(QString appearanceMode READ appearanceMode WRITE setAppearanceMode NOTIFY preferencesChanged)
    Q_PROPERTY(QString themePreset READ themePreset WRITE setThemePreset NOTIFY preferencesChanged)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY preferencesChanged)
    Q_PROPERTY(QString root READ root CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(bool canRestoreAvatar READ canRestoreAvatar NOTIFY avatarsChanged)
public:
    explicit WorkspaceLibrary(QString outputRoot, QObject *parent = nullptr);
    QVariantList avatars() const { return m_avatars; }
    QString root() const { return m_root; }
    QString version() const { return QStringLiteral("0.1.0"); }
    QString appearanceMode() const { return m_appearance; }
    QString themePreset() const { return m_theme; }
    QString language() const { return m_language; }
    void setAppearanceMode(QString value);
    void setThemePreset(QString value);
    void setLanguage(QString value);
    bool canRestoreAvatar() const { return !m_archived.isEmpty(); }
    Q_INVOKABLE QString importAvatar(QUrl source, QString name);
    Q_INVOKABLE QString renameAvatar(QString id, QString name);
    Q_INVOKABLE QString removeAvatar(QString id);
    Q_INVOKABLE QString restoreAvatar();
    Q_INVOKABLE void openDirectory(QString path);
    Q_INVOKABLE QString notices() const;
    Q_INVOKABLE QString noticesHtml(const QColor &linkColor) const;
signals:
    void avatarsChanged();
    void preferencesChanged();
private:
    bool save(const QVariantList &avatars, const QVariantList &archived) const;
    QString m_root, m_appearance, m_theme, m_language;
    QVariantList m_avatars, m_archived;
};
