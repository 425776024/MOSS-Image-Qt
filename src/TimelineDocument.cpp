#include "TimelineDocument.h"

#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QUrl>
#include <algorithm>
#include <cmath>

namespace {
constexpr double minimumClipDuration = .1;
constexpr double maximumTimelineDuration = 600.;

double finiteNumber(const QVariant &value, double fallback)
{
    if (value.metaType().id() == QMetaType::Bool)
        return fallback;
    bool ok = false;
    const double result = value.toDouble(&ok);
    return ok && std::isfinite(result) ? result : fallback;
}

QString sourceUrl(const QVariant &value)
{
    if (value.metaType().id() != QMetaType::QString
        && value.metaType().id() != QMetaType::QUrl)
        return {};
    const auto source = value.toString().trimmed();
    if (source.isEmpty())
        return {};
    if (QDir::isAbsolutePath(source))
        return QUrl::fromLocalFile(QDir::cleanPath(source)).toString();
    const QUrl url(source);
    return url.isValid() && !url.scheme().isEmpty() ? url.toString() : QString();
}

QString inferredKind(const QString &source, const QString &fallback)
{
    const auto suffix = QFileInfo(QUrl(source).path()).suffix().toLower();
    if (QStringList{"mp4", "mkv", "mov", "webm", "m4v", "avi"}.contains(suffix))
        return QStringLiteral("video");
    if (QStringList{"wav", "mp3", "m4a", "aac", "flac", "ogg", "aiff", "aif"}.contains(suffix))
        return QStringLiteral("audio");
    if (QStringList{"png", "jpg", "jpeg", "webp", "bmp", "gif", "tif", "tiff", "svg"}.contains(suffix))
        return QStringLiteral("image");
    return fallback;
}

QString validKind(const QVariant &input, const QString &fallback)
{
    const auto kind = input.toString();
    return QStringList{"image", "video", "audio", "color"}.contains(kind)
        ? kind : fallback;
}
}

TimelineDocument::TimelineDocument(QObject *parent)
    : QObject(parent), m_tracks(defaultTracks()), m_durationProbe(this)
{
    connect(&m_durationProbe, &AudioDurationProbe::durationReady, this,
            [this](const QString &key, const QString &source, double seconds) {
        if (audioSource(track(key)) == source)
            setMediaDuration(key, seconds);
    });
}

QVariantList TimelineDocument::defaultTracks()
{
    QVariantList result;
    const QList<QStringList> definitions = {
        {"image", "文生图", "image"},
        {"speech", "数字人音频", "audio"},
        {"sfx", "音效生成", "audio"},
        {"background", "背景", "color"}
    };
    for (const auto &definition : definitions) {
        result.append(QVariantMap{
            {"key", definition[0]}, {"name", definition[1]},
            {"start", 0.}, {"duration", 30.}, {"source", QString()},
            {"audioSource", QString()},
            {"mediaDuration", 0.}, {"rangeEdited", false},
            {"kind", definition[2]}, {"enabled", true},
            {"x", 0.}, {"y", 0.}, {"width", 1.}, {"height", 1.},
            {"color", "#202427"}, {"gain", 1.}
        });
    }
    return result;
}

int TimelineDocument::indexOf(const QString &key) const
{
    for (qsizetype i = 0; i < m_tracks.size(); ++i) {
        if (m_tracks.at(i).toMap().value("key").toString() == key)
            return int(i);
    }
    return -1;
}

QVariantMap TimelineDocument::track(const QString &key) const
{
    const auto index = indexOf(key);
    auto row = index >= 0 ? m_tracks.at(index).toMap() : QVariantMap{};
    if (key == "background" && !row.isEmpty()) {
        row["start"] = 0.;
        row["duration"] = duration();
    }
    return row;
}

QVariantList TimelineDocument::tracks() const
{
    return {track("speech"), track("sfx"), track("background")};
}

QString TimelineDocument::audioSource(const QVariantMap &row)
{
    if (row.value("key") != "speech" && row.value("key") != "sfx")
        return {};
    if (row.value("kind") == "video" && !row.value("audioSource").toString().isEmpty())
        return row.value("audioSource").toString();
    return row.value("source").toString();
}

double TimelineDocument::duration() const
{
    double end = minimumClipDuration;
    bool hasMaterial = false;
    for (const auto &value : m_tracks) {
        const auto row = value.toMap();
        if (audioSource(row).isEmpty())
            continue;
        hasMaterial = true;
        end = std::max(end, row.value("start").toDouble() + row.value("duration").toDouble());
    }
    return hasMaterial ? end : 30.;
}

void TimelineDocument::setSelectedTrack(const QString &key)
{
    if (indexOf(key) < 0 || m_selectedTrack == key)
        return;
    m_selectedTrack = key;
    emit selectionChanged();
    emit selectedChanged();
}

void TimelineDocument::setPlayhead(double seconds)
{
    if (!std::isfinite(seconds))
        return;
    seconds = std::clamp(seconds, 0., duration());
    if (m_playhead == seconds)
        return;
    m_playhead = seconds;
    emit playheadChanged();
}

void TimelineDocument::replace(int index, const QVariantMap &row)
{
    if (index < 0 || m_tracks.at(index).toMap() == row)
        return;
    m_tracks[index] = row;
    emit changed();
    if (row.value("key").toString() == m_selectedTrack || m_selectedTrack == "background")
        emit selectedChanged();
    setPlayhead(m_playhead);
}

void TimelineDocument::setRange(const QString &key, double start, double length)
{
    const auto index = indexOf(key);
    if ((key != "speech" && key != "sfx") || index < 0 || !std::isfinite(start) || !std::isfinite(length))
        return;
    auto row = m_tracks.at(index).toMap();
    start = std::clamp(start, 0., maximumTimelineDuration - minimumClipDuration);
    length = std::clamp(length, minimumClipDuration, maximumTimelineDuration - start);
    if (length != row.value("duration").toDouble())
        row.insert("rangeEdited", true);
    row.insert("start", start);
    row.insert("duration", length);
    replace(index, row);
}

void TimelineDocument::setTransform(const QString &key, double x, double y,
                                    double width, double height)
{
    const auto index = indexOf(key);
    if (key == "image" || index < 0 || !std::isfinite(x) || !std::isfinite(y)
        || !std::isfinite(width) || !std::isfinite(height))
        return;
    auto row = m_tracks.at(index).toMap();
    row.insert("x", std::clamp(x, -2., 2.));
    row.insert("y", std::clamp(y, -2., 2.));
    row.insert("width", std::clamp(width, .02, 4.));
    row.insert("height", std::clamp(height, .02, 4.));
    replace(index, row);
}

void TimelineDocument::setSource(const QString &key, const QString &url, const QString &kind)
{
    const auto index = indexOf(key);
    if (index < 0)
        return;
    auto row = m_tracks.at(index).toMap();
    const auto source = sourceUrl(url);
    // Reject malformed input rather than accidentally replacing a usable asset.
    if (!url.trimmed().isEmpty() && source.isEmpty())
        return;
    row.insert("source", source);
    row.insert("kind", validKind(kind, inferredKind(source, row.value("kind").toString())));
    replaceSource(index, row);
}

void TimelineDocument::setValue(const QString &key, const QString &field, const QVariant &value)
{
    const auto index = indexOf(key);
    if (index < 0)
        return;
    auto row = m_tracks.at(index).toMap();
    if (field == "enabled" && value.metaType().id() == QMetaType::Bool) {
        row.insert(field, value.toBool());
    } else if (field == "gain") {
        const double gain = finiteNumber(value, row.value("gain").toDouble());
        row.insert(field, std::clamp(gain, 0., 2.));
    } else if (field == "audioSource" && key == "speech") {
        const auto source = sourceUrl(value);
        if (!value.toString().trimmed().isEmpty() && source.isEmpty())
            return;
        row.insert(field, source);
        replaceSource(index, row);
        return;
    } else if (field == "color") {
        const QColor color(value.toString());
        if (!color.isValid())
            return;
        row.insert(field, color.name(QColor::HexRgb));
    } else {
        return;
    }
    replace(index, row);
}

void TimelineDocument::resetTransform(const QString &key)
{
    setTransform(key, 0., 0., 1., 1.);
}

void TimelineDocument::replaceSource(int index, QVariantMap row)
{
    const auto previous = m_tracks.at(index).toMap();
    const auto source = audioSource(row);
    if (source != audioSource(previous)) {
        row["mediaDuration"] = 0.;
        row["rangeEdited"] = false;
        row["duration"] = std::clamp(30., minimumClipDuration,
                                    maximumTimelineDuration - row.value("start").toDouble());
    }
    replace(index, row);
    m_durationProbe.setSource(row.value("key").toString(), source);
}

void TimelineDocument::setMediaDuration(const QString &key, double seconds)
{
    const auto index = indexOf(key);
    if (index < 0 || (key != "speech" && key != "sfx") || !std::isfinite(seconds) || seconds <= 0)
        return;
    auto row = m_tracks.at(index).toMap();
    if (audioSource(row).isEmpty())
        return;
    if (row.value("mediaDuration").toDouble() <= 0 && !row.value("rangeEdited").toBool())
        row["duration"] = std::clamp(seconds, minimumClipDuration,
                                     maximumTimelineDuration - row.value("start").toDouble());
    row["mediaDuration"] = seconds;
    replace(index, row);
}

void TimelineDocument::probeSources()
{
    for (const auto &key : {"speech", "sfx"})
        m_durationProbe.setSource(key, audioSource(track(key)));
}

QJsonObject TimelineDocument::snapshot() const
{
    return {{"version", 1}, {"tracks", QJsonArray::fromVariantList(m_tracks)}};
}

QVariantMap TimelineDocument::normalize(const QVariantMap &input, const QVariantMap &defaults)
{
    auto row = defaults;
    const double start = std::clamp(finiteNumber(input.value("start"), 0.), 0.,
                                    maximumTimelineDuration - minimumClipDuration);
    row.insert("start", start);
    row.insert("duration", std::clamp(finiteNumber(input.value("duration"), 30.),
                                     minimumClipDuration, maximumTimelineDuration - start));
    row.insert("source", sourceUrl(input.value("source")));
    if (defaults.value("key").toString() == "speech")
        row.insert("audioSource", sourceUrl(input.value("audioSource")));
    row.insert("kind", validKind(input.value("kind"),
                               inferredKind(row.value("source").toString(),
                                            defaults.value("kind").toString())));
    if (input.value("enabled").metaType().id() == QMetaType::Bool)
        row.insert("enabled", input.value("enabled").toBool());
    for (const auto &field : {"x", "y"})
        row.insert(field, std::clamp(finiteNumber(input.value(field), 0.), -2., 2.));
    for (const auto &field : {"width", "height"})
        row.insert(field, std::clamp(finiteNumber(input.value(field), 1.), .02, 4.));
    row.insert("gain", std::clamp(finiteNumber(input.value("gain"), 1.), 0., 2.));
    row.insert("mediaDuration", std::max(0., finiteNumber(input.value("mediaDuration"), 0.)));
    row.insert("rangeEdited", input.value("rangeEdited").metaType().id() == QMetaType::Bool
                                  ? input.value("rangeEdited").toBool()
                                  : row.value("duration").toDouble() != 30.);
    const QColor color(input.value("color").toString());
    if (color.isValid())
        row.insert("color", color.name(QColor::HexRgb));
    if (defaults.value("key") == "image") {
        // Earlier projects allowed image clips to move and trim. They now
        // restore as the fixed canvas without losing the generated source.
        for (const auto &field : {"start", "duration", "x", "y", "width", "height"})
            row.insert(field, defaults.value(field));
        row["rangeEdited"] = false;
        row["mediaDuration"] = 0.;
    }
    if (defaults.value("key") == "background") {
        row["start"] = 0.;
        row["duration"] = 30.;
        row["rangeEdited"] = false;
        row["mediaDuration"] = 0.;
    }
    return row;
}

void TimelineDocument::restore(const QJsonObject &json)
{
    m_durationProbe.reset();
    auto rows = defaultTracks();
    // Unknown fields and unknown tracks never escape the document boundary.
    const auto input = json.value("tracks").toArray();
    for (qsizetype i = 0; i < rows.size(); ++i) {
        const auto defaults = rows.at(i).toMap();
        for (const auto &value : input) {
            const auto candidate = value.toObject().toVariantMap();
            if (candidate.value("key") == defaults.value("key")) {
                rows[i] = normalize(candidate, defaults);
                break;
            }
        }
    }
    const bool differs = rows != m_tracks;
    m_tracks = rows;
    setSelectedTrack("image");
    setPlayhead(0.);
    if (differs) {
        emit changed();
        emit selectedChanged();
    }
    probeSources();
}
