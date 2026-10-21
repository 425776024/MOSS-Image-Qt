#pragma once

#include <QObject>
#include <QHash>
#include <QPointer>

class QMediaPlayer;

// Reads audio metadata without an audio output or playback. Resetting a page
// invalidates pending metadata even when its replacement uses the same URL.
class AudioDurationProbe : public QObject {
    Q_OBJECT
public:
    explicit AudioDurationProbe(QObject *parent = nullptr) : QObject(parent) {}
    void setSource(const QString &key, const QString &source);
    void reset();
    static double waveDuration(const QString &path);

signals:
    void durationReady(QString key, QString source, double seconds);

private:
    struct Entry {
        QString source;
        double seconds = 0;
        QPointer<QMediaPlayer> player;
    };
    QHash<QString, Entry> m_entries;
    quint64 m_epoch = 0;
};
