#pragma once
#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QUrl>
#include <QColor>

// Home-library files are owned by Moss, independently of the source material.
class WorkspaceLibrary : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList avatars READ avatars NOTIFY avatarsChanged)
    Q_PROPERTY(QVariantList builtinAvatars READ builtinAvatars NOTIFY preferencesChanged)
    Q_PROPERTY(QString appearanceMode READ appearanceMode WRITE setAppearanceMode NOTIFY preferencesChanged)
    Q_PROPERTY(QString themePreset READ themePreset WRITE setThemePreset NOTIFY preferencesChanged)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY preferencesChanged)
    Q_PROPERTY(QString languagePreference READ languagePreference NOTIFY preferencesChanged)
    Q_PROPERTY(QString appName READ appName NOTIFY preferencesChanged)
    Q_PROPERTY(bool quickStartPending READ quickStartPending NOTIFY preferencesChanged)
    Q_PROPERTY(QString root READ root CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(bool canRestoreAvatar READ canRestoreAvatar NOTIFY avatarsChanged)
public:
    explicit WorkspaceLibrary(QString outputRoot, QObject *parent = nullptr);
    QVariantList avatars() const { return m_avatars; }
    QVariantList builtinAvatars() const;
    QString root() const { return m_root; }
    QString version() const { return QStringLiteral("0.1.0"); }
    QString appearanceMode() const { return m_appearance; }
    QString themePreset() const { return m_theme; }
    QString language() const { return m_language; }
    QString languagePreference() const { return m_languagePreference; }
    QString appName() const { return m_language == "zh" ? QStringLiteral("新身数字人") : QStringLiteral("XAvatar"); }
    bool quickStartPending() const;
    Q_INVOKABLE void completeQuickStart();
    static QString resolveLanguage(const QStringList &uiLanguages);
    void setAppearanceMode(QString value);
    void setThemePreset(QString value);
    void setLanguage(QString value);
    void setSessionLanguage(QString value);
    bool canRestoreAvatar() const { return !m_archived.isEmpty(); }
    Q_INVOKABLE QString importAvatar(QUrl source, QString name,QVariantMap generationRecipe={});
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
    QString m_root, m_appearance, m_theme, m_language, m_languagePreference;
    QVariantList m_avatars, m_archived;
};
