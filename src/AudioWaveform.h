#pragma once
#include <QQuickPaintedItem>
#include <QAudioBuffer>
#include <QCache>
#include <QQueue>
#include <QUrl>
#include <QtQml/qqmlregistration.h>
class QAudioDecoder;
class QTimer;

// Full-duration, bounded peak envelope. Stereo channels use the maximum
// magnitude, so opposite-phase signals do not disappear from the overview.
struct WaveformPeaks {
    QVector<float> peaks;
    qint64 frames=0, framesPerPeak=256;
    int sampleRate=0;
    QString error;
    void append(const QAudioBuffer &buffer);
    QVector<float> bars(int count) const;
    double seconds() const { return sampleRate>0 ? double(frames)/sampleRate : 0; }
};
class WaveformCache : public QObject {
    Q_OBJECT
public:
    explicit WaveformCache(QObject *parent=nullptr,int capacity=128);
    static WaveformCache *instance();
    static QString key(const QUrl &source);
    void request(const QUrl &source);
    const WaveformPeaks *get(const QString &key) const { return m_cache.object(key); }
    int entryCount() const { return m_cache.size(); }
    int decodeCount() const { return m_decodeCount; }
signals:
    void ready(QString key);
private:
    void next();
    void complete(QString error={});
    QCache<QString,WaveformPeaks> m_cache;
    QQueue<QPair<QString,QUrl>> m_queue;
    QString m_current;
    QAudioDecoder *m_decoder=nullptr;
    QTimer *m_timeout;
    WaveformPeaks m_build;
    int m_decodeCount=0;
};
class AudioWaveform : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(qreal progress READ progress WRITE setProgress NOTIFY progressChanged)
    Q_PROPERTY(QColor waveColor MEMBER m_waveColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor playedColor MEMBER m_playedColor NOTIFY colorsChanged)
    Q_PROPERTY(double seconds READ seconds NOTIFY dataChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY dataChanged)
    Q_PROPERTY(QString error READ error NOTIFY dataChanged)
public:
    explicit AudioWaveform(QQuickItem *parent=nullptr);
    QUrl source() const { return m_source; }
    void setSource(QUrl source);
    bool active() const { return m_active; }
    void setActive(bool active);
    qreal progress() const { return m_progress; }
    void setProgress(qreal progress);
    double seconds() const { return m_data.seconds(); }
    bool ready() const { return !m_data.peaks.isEmpty(); }
    QString error() const { return m_data.error; }
    void paint(QPainter *painter) override;
signals:
    void sourceChanged();
    void activeChanged();
    void progressChanged();
    void colorsChanged();
    void dataChanged();
private:
    void refresh();
    QUrl m_source;
    QString m_key;
    WaveformPeaks m_data;
    bool m_active=true;
    qreal m_progress=0;
    QColor m_waveColor="#b4bdc9",m_playedColor="#007aff";
};
