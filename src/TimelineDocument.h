#pragma once

#include <QObject>
#include <QJsonObject>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include "AudioDurationProbe.h"

// A page has a fixed image canvas and three editable tracks. The image entry
// remains internal for project compatibility and canvas-inspector selection.
// Playback position and selection never cause the page to be saved.
class TimelineDocument : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(QVariantList tracks READ tracks NOTIFY changed)
    Q_PROPERTY(QString selectedTrack READ selectedTrack WRITE setSelectedTrack NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap selected READ selected NOTIFY selectedChanged)
    Q_PROPERTY(double playhead READ playhead WRITE setPlayhead NOTIFY playheadChanged)
    Q_PROPERTY(double duration READ duration NOTIFY changed)
public:
    explicit TimelineDocument(QObject *parent = nullptr);

    QVariantList tracks() const;
    QString selectedTrack() const { return m_selectedTrack; }
    QVariantMap selected() const { return track(m_selectedTrack); }
    double playhead() const { return m_playhead; }
    double duration() const;
    void setSelectedTrack(const QString &key);
    void setPlayhead(double seconds);

    Q_INVOKABLE QVariantMap track(const QString &key) const;
    Q_INVOKABLE void select(const QString &key) { setSelectedTrack(key); }
    Q_INVOKABLE void setRange(const QString &key, double start, double duration);
    Q_INVOKABLE void setTransform(const QString &key, double x, double y,
                                  double width, double height);
    Q_INVOKABLE void setSource(const QString &key, const QString &url,
                              const QString &kind = QString());
    Q_INVOKABLE void setValue(const QString &key, const QString &field,
                              const QVariant &value);
    Q_INVOKABLE void resetTransform(const QString &key);
    Q_INVOKABLE void setMediaDuration(const QString &key, double seconds);

    QJsonObject snapshot() const;
    void restore(const QJsonObject &json);

signals:
    void changed();
    void selectionChanged();
    void selectedChanged();
    void playheadChanged();

private:
    static QVariantList defaultTracks();
    static QVariantMap normalize(const QVariantMap &input, const QVariantMap &defaults);
    int indexOf(const QString &key) const;
    void replace(int index, const QVariantMap &row);
    void replaceSource(int index, QVariantMap row);
    static QString audioSource(const QVariantMap &row);
    void probeSources();

    QVariantList m_tracks;
    AudioDurationProbe m_durationProbe;
    QString m_selectedTrack = QStringLiteral("image");
    double m_playhead = 0;
};
