#include "AudioDurationProbe.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMediaPlayer>
#include <QUrl>
#include <QtEndian>
#include <cmath>
#include <utility>

double AudioDurationProbe::waveDuration(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return 0;
    const auto header = file.read(12);
    if (header.size() != 12 || header.first(4) != "RIFF" || header.mid(8, 4) != "WAVE")
        return 0;
    quint32 byteRate = 0;
    quint64 dataBytes = 0;
    for (int chunks = 0; chunks < 4096 && file.pos() + 8 <= file.size(); ++chunks) {
        const auto chunk = file.read(8);
        if (chunk.size() != 8)
            return 0;
        const auto size = qFromLittleEndian<quint32>(chunk.constData() + 4);
        const auto start = file.pos();
        if (size > quint64(file.size() - start))
            return 0; // A truncated recording cannot claim unwritten samples.
        if (chunk.first(4) == "fmt " && size >= 16) {
            const auto format = file.read(16);
            if (format.size() != 16)
                return 0;
            const auto encoding = qFromLittleEndian<quint16>(format.constData());
            if (encoding != 1 && encoding != 3 && encoding != 0xfffe)
                return 0; // Compressed WAVs use the multimedia metadata fallback.
            byteRate = qFromLittleEndian<quint32>(format.constData() + 8);
        } else if (chunk.first(4) == "data") {
            dataBytes += size;
        }
        if (!file.seek(start + size + (size & 1)))
            return 0;
    }
    return byteRate > 0 && dataBytes > 0 ? double(dataBytes) / byteRate : 0;
}

void AudioDurationProbe::reset()
{
    ++m_epoch;
    for (const auto &entry : std::as_const(m_entries)) {
        if (entry.player) {
            entry.player->disconnect(this);
            entry.player->deleteLater();
        }
    }
    m_entries.clear();
}

void AudioDurationProbe::setSource(const QString &key, const QString &source)
{
    if (key != "speech" && key != "sfx")
        return;
    const QUrl url = QDir::isAbsolutePath(source) ? QUrl::fromLocalFile(source) : QUrl(source);
    const auto normalized = source.isEmpty() ? QString() : url.toString();
    auto previous = m_entries.find(key);
    if (previous != m_entries.end() && previous->source == normalized) {
        if (previous->seconds > 0)
            emit durationReady(key, normalized, previous->seconds);
        return;
    }
    if (previous != m_entries.end() && previous->player) {
        previous->player->disconnect(this);
        previous->player->deleteLater();
    }
    m_entries.insert(key, Entry{normalized});
    if (normalized.isEmpty() || !url.isValid()
        || (url.isLocalFile() && !QFileInfo(url.toLocalFile()).isFile()))
        return;
    if (url.isLocalFile()) {
        const auto seconds = waveDuration(url.toLocalFile());
        if (seconds > 0 && std::isfinite(seconds)) {
            m_entries[key].seconds = seconds;
            emit durationReady(key, normalized, seconds);
            return;
        }
    }
    const auto epoch = m_epoch;
    auto player = new QMediaPlayer(this);
    m_entries[key].player = player;
    connect(player, &QMediaPlayer::durationChanged, this,
            [this, key, normalized, epoch, player](qint64 milliseconds) {
        auto current = m_entries.find(key);
        if (epoch != m_epoch || current == m_entries.end()
            || current->source != normalized || current->player != player || milliseconds <= 0)
            return;
        const double seconds = milliseconds / 1000.;
        current->seconds = seconds;
        emit durationReady(key, normalized, seconds);
    });
    // No audioOutput is attached and play() is never called.
    player->setSource(url);
}
