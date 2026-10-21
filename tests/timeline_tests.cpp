#include "TimelineDocument.h"

#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QDataStream>
#include <QTemporaryDir>
#include <QMediaPlayer>
#include <limits>

class TimelineTest : public QObject {
    Q_OBJECT
private:
    static bool wave(const QString &path, double seconds, int rate = 16000, int channels = 1)
    {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) return false;
        const quint32 bytes = quint32(seconds * rate) * channels * 2;
        QDataStream out(&file);out.setByteOrder(QDataStream::LittleEndian);
        out.writeRawData("RIFF",4);out << quint32(36 + bytes);
        out.writeRawData("WAVEfmt ",8);
        out << quint32(16) << quint16(1) << quint16(channels) << quint32(rate)
            << quint32(rate * channels * 2) << quint16(channels * 2) << quint16(16);
        out.writeRawData("data",4);out << bytes;
        const QByteArray samples(bytes, '\0');
        out.writeRawData(samples.constData(), samples.size());
        return out.status() == QDataStream::Ok;
    }
private slots:
    void threeEditableTracksAndFixedCanvasDefaultToThirtySeconds()
    {
        TimelineDocument document;
        QCOMPARE(document.tracks().size(), 3);
        QCOMPARE(document.duration(), 30.);
        QCOMPARE(document.selectedTrack(), QString("image"));
        const QStringList keys{"speech", "sfx", "background"};
        for (int i = 0; i < keys.size(); ++i) {
            const auto row = document.tracks().at(i).toMap();
            QCOMPARE(row.value("key").toString(), keys[i]);
            QCOMPARE(row.value("start").toDouble(), 0.);
            QCOMPARE(row.value("duration").toDouble(), 30.);
            QVERIFY(row.value("enabled").toBool());
            QVERIFY(row.value("source").toString().isEmpty());
        }
        QCOMPARE(document.selected().value("key").toString(), QString("image"));
        QCOMPARE(document.track("image").value("duration").toDouble(), 30.);
        QCOMPARE(document.track("background").value("kind").toString(), QString("color"));
    }

    void rangeStaysFiniteAndWithinTimeline()
    {
        TimelineDocument document;
        document.setSource("speech", "file:///tmp/voice.wav");
        document.setRange("speech", 7.5, 35.);
        QCOMPARE(document.duration(), 42.5);
        document.setRange("speech", -5., -2.);
        QCOMPARE(document.track("speech").value("start").toDouble(), 0.);
        QCOMPARE(document.track("speech").value("duration").toDouble(), .1);
        document.setRange("speech", 700., 800.);
        QCOMPARE(document.track("speech").value("start").toDouble(), 599.9);
        QCOMPARE(document.duration(), 600.);
        QVERIFY(document.track("speech").value("duration").toDouble() >= .099999);
        const auto snapshot = document.snapshot();
        document.setRange("speech", std::numeric_limits<double>::quiet_NaN(), 3.);
        document.setRange("speech", 0., std::numeric_limits<double>::infinity());
        document.setRange("unknown", 10., 2.);
        QCOMPARE(document.snapshot(), snapshot);
    }

    void transformsClampAndRejectInvalidNumbers()
    {
        TimelineDocument document;
        document.setTransform("background", -4., 5., -1., 100.);
        const auto row = document.track("background");
        QCOMPARE(row.value("x").toDouble(), -2.);
        QCOMPARE(row.value("y").toDouble(), 2.);
        QCOMPARE(row.value("width").toDouble(), .02);
        QCOMPARE(row.value("height").toDouble(), 4.);
        const auto snapshot = document.snapshot();
        document.setTransform("background", 0., 0., std::numeric_limits<double>::infinity(), 1.);
        document.setTransform("background", std::numeric_limits<double>::quiet_NaN(), 0., 1., 1.);
        QCOMPARE(document.snapshot(), snapshot);
        document.resetTransform("background");
        QCOMPARE(document.track("background").value("x").toDouble(), 0.);
        QCOMPARE(document.track("background").value("width").toDouble(), 1.);
    }

    void selectionAndScrubbingDoNotPersist()
    {
        TimelineDocument document;
        document.setSource("speech", "file:///tmp/audio.wav");
        QSignalSpy edits(&document, &TimelineDocument::changed);
        QSignalSpy selections(&document, &TimelineDocument::selectionChanged);
        QSignalSpy positions(&document, &TimelineDocument::playheadChanged);
        document.select("speech");
        document.select("missing");
        document.setPlayhead(5.);
        document.setPlayhead(std::numeric_limits<double>::infinity());
        QCOMPARE(document.selectedTrack(), QString("speech"));
        QCOMPARE(document.selected().value("key").toString(), QString("speech"));
        QCOMPARE(selections.size(), 1);
        QCOMPARE(positions.size(), 1);
        QCOMPARE(edits.size(), 0);
        document.setPlayhead(100.);
        QCOMPARE(document.playhead(), 30.);
        document.setRange("speech", 30., 50.);
        document.setPlayhead(70.);
        document.setRange("speech", 0., 20.);
        QCOMPARE(document.playhead(), 20.);
        QCOMPARE(edits.size(), 2);
    }

    void durationFollowsAudioRangesIndependentlyOfMuteAndBackground()
    {
        TimelineDocument document;
        document.setRange("background", 0., 5.);
        document.setRange("speech", 60., 10.);
        QCOMPARE(document.duration(), 30.); // Empty clips and background never extend duration.
        document.setSource("speech", "file:///tmp/voice.wav");
        QCOMPARE(document.duration(), 90.); // New metadata is still pending.
        document.setRange("speech", 60., 10.);
        QCOMPARE(document.duration(), 70.);
        QCOMPARE(document.track("background").value("start").toDouble(), 0.);
        QCOMPARE(document.track("background").value("duration").toDouble(), 70.);
        document.setPlayhead(60.);
        document.setValue("speech", "enabled", false);
        QCOMPARE(document.duration(), 70.);
        QCOMPARE(document.playhead(), 60.);
        document.setValue("background", "enabled", false);
        QCOMPARE(document.duration(), 70.);
        document.setSource("speech", "");
        QCOMPARE(document.duration(), 30.); // Empty editing canvas remains usable.
        document.setSource("sfx", "file:///tmp/effect.wav");
        document.setRange("sfx", 2., 3.);
        QCOMPARE(document.duration(), 5.);
        document.setRange("sfx", 0., .1);
        QCOMPARE(document.duration(), .1);
    }

    void canvasCannotBeMovedTrimmedOrExtendTheTimeline()
    {
        TimelineDocument document;
        document.setSource("image", "file:///tmp/canvas.png");
        document.setSource("speech", "file:///tmp/voice.wav");
        document.setRange("speech", 0., 5.);
        const auto snapshot = document.snapshot();
        QSignalSpy edits(&document, &TimelineDocument::changed);
        document.setRange("image", 80., 120.);
        document.setTransform("image", .2, .1, .4, .6);
        document.resetTransform("image");
        QCOMPARE(document.snapshot(), snapshot);
        QCOMPARE(document.duration(), 5.);
        QCOMPARE(edits.size(), 0);
        document.select("speech");
        document.select("image");
        QCOMPARE(document.selected().value("source").toString(), QString("file:///tmp/canvas.png"));
        for (const auto &track : document.tracks())
            QVERIFY(track.toMap().value("key") != "image");
    }

    void legacyCanvasGeometryResetsWithoutLosingSource()
    {
        TimelineDocument document;
        document.restore({{"tracks", QJsonArray{
            QJsonObject{{"key", "image"}, {"source", "file:///tmp/old-canvas.png"},
                        {"start", 90.}, {"duration", 120.}, {"x", .2}, {"y", .3},
                        {"width", .4}, {"height", .5}},
            QJsonObject{{"key", "speech"}, {"source", "file:///tmp/voice.wav"}, {"duration", 4.}}
        }}});
        const auto canvas = document.track("image");
        QCOMPARE(canvas.value("source").toString(), QString("file:///tmp/old-canvas.png"));
        QCOMPARE(canvas.value("start").toDouble(), 0.);
        QCOMPARE(canvas.value("duration").toDouble(), 30.);
        QCOMPARE(canvas.value("x").toDouble(), 0.);
        QCOMPARE(canvas.value("y").toDouble(), 0.);
        QCOMPARE(canvas.value("width").toDouble(), 1.);
        QCOMPARE(canvas.value("height").toDouble(), 1.);
        QCOMPARE(document.duration(), 4.);
        TimelineDocument reopened;
        reopened.restore(document.snapshot());
        QCOMPARE(reopened.snapshot(), document.snapshot());
    }

    void actualWaveDurationIsImmediateAndManualTrimSurvivesReload()
    {
        QTemporaryDir dir;
        const auto first = dir.filePath("voice.wav"), second = dir.filePath("effect.wav");
        QVERIFY(wave(first, 2.5));
        QVERIFY(wave(second, 6.75, 44100, 2));
        QCOMPARE(AudioDurationProbe::waveDuration(first), 2.5);
        QCOMPARE(AudioDurationProbe::waveDuration(second), 6.75);
        TimelineDocument document;
        document.setSource("speech", first);
        QCOMPARE(document.track("speech").value("mediaDuration").toDouble(), 2.5);
        QCOMPARE(document.duration(), 2.5);
        document.setRange("speech", 3., 1.);
        QCOMPARE(document.duration(), 4.);
        document.setSource("speech", first);
        QCOMPARE(document.track("speech").value("duration").toDouble(), 1.);
        const auto saved = document.snapshot();
        TimelineDocument reopened;
        reopened.restore(saved);
        QCOMPARE(reopened.snapshot(), saved);
        QCOMPARE(reopened.duration(), 4.);
        reopened.setSource("speech", second);
        QCOMPARE(reopened.track("speech").value("start").toDouble(), 3.);
        QCOMPARE(reopened.track("speech").value("duration").toDouble(), 6.75);
        QCOMPARE(reopened.duration(), 9.75);
    }

    void creatingVideoForSameNarrationKeepsTrimAndChangingNarrationResetsIt()
    {
        QTemporaryDir dir;
        const auto first = dir.filePath("first.wav"), second = dir.filePath("second.wav");
        QVERIFY(wave(first, 3.));
        QVERIFY(wave(second, 5.));
        TimelineDocument document;
        document.setSource("speech", first, "audio");
        document.setValue("speech", "audioSource", first);
        document.setRange("speech", 2., 1.5);
        document.setSource("speech", dir.filePath("talking.mp4"), "video");
        QCOMPARE(document.track("speech").value("duration").toDouble(), 1.5);
        QCOMPARE(document.track("speech").value("mediaDuration").toDouble(), 3.);
        document.setValue("speech", "audioSource", second);
        QCOMPARE(document.track("speech").value("duration").toDouble(), 5.);
        QCOMPARE(document.duration(), 7.);
    }

    void pendingMetadataDoesNotOverrideUserTrimAndInvalidDurationsAreRejected()
    {
        TimelineDocument document;
        document.setSource("sfx", "file:///tmp/pending-metadata.mp3");
        document.setRange("sfx", 2., 4.);
        document.setMediaDuration("sfx", 12.);
        QCOMPARE(document.track("sfx").value("mediaDuration").toDouble(), 12.);
        QCOMPARE(document.track("sfx").value("duration").toDouble(), 4.);
        QCOMPARE(document.duration(), 6.);
        const auto saved = document.snapshot();
        document.setMediaDuration("sfx", -1.);
        document.setMediaDuration("sfx", std::numeric_limits<double>::infinity());
        document.setMediaDuration("image", 80.);
        document.setMediaDuration("background", 80.);
        QCOMPARE(document.snapshot(), saved);
        document.setSource("sfx", "file:///tmp/replaced-metadata.mp3");
        QCOMPARE(document.track("sfx").value("duration").toDouble(), 30.);
        QCOMPARE(document.track("sfx").value("mediaDuration").toDouble(), 0.);
        document.setMediaDuration("sfx", 7.);
        QCOMPARE(document.duration(), 9.);
    }

    void waveProbeRejectsTruncatedRecordingsAndHandlesSourceReplacement()
    {
        QTemporaryDir dir;
        const auto first = dir.filePath("one.wav"), second = dir.filePath("two.wav");
        QVERIFY(wave(first, 1.));
        QVERIFY(wave(second, 2.));
        AudioDurationProbe probe;
        QSignalSpy ready(&probe, &AudioDurationProbe::durationReady);
        probe.setSource("speech", first);
        probe.reset();
        probe.setSource("speech", second);
        QCOMPARE(ready.size(), 2);
        QCOMPARE(ready.last().at(1).toString(), QUrl::fromLocalFile(second).toString());
        QCOMPARE(ready.last().at(2).toDouble(), 2.);
        QFile truncated(first);QVERIFY(truncated.open(QIODevice::ReadWrite));
        QVERIFY(truncated.resize(46));truncated.close();
        QCOMPARE(AudioDurationProbe::waveDuration(first), 0.);
        TimelineDocument document;
        document.setSource("speech", second);
        document.restore({});
        QCoreApplication::processEvents();
        QVERIFY(document.track("speech").value("source").toString().isEmpty());
        QCOMPARE(document.track("speech").value("mediaDuration").toDouble(), 0.);
        QCOMPARE(document.duration(), 30.);
    }

    void queuedMetadataFromPreviousPageCannotChangeCurrentPage()
    {
        QTemporaryDir dir;
        const auto pending = dir.filePath("pending.mp3"), current = dir.filePath("current.wav");
        { QFile file(pending);QVERIFY(file.open(QIODevice::WriteOnly));file.write("metadata pending fixture"); }
        QVERIFY(wave(current, 2.));
        TimelineDocument document;
        document.setSource("speech", pending);
        auto player = document.findChild<QMediaPlayer *>();
        QVERIFY(player);
        QVERIFY(QMetaObject::invokeMethod(player, "durationChanged", Qt::QueuedConnection,
                                        Q_ARG(qint64, 19000)));
        document.restore({});
        document.setSource("speech", current);
        document.setRange("speech", 0., 1.);
        QCoreApplication::processEvents();
        QCOMPARE(document.track("speech").value("mediaDuration").toDouble(), 2.);
        QCOMPARE(document.track("speech").value("duration").toDouble(), 1.);
        QCOMPARE(document.duration(), 1.);
    }

    void compressedAudioMetadataLoadsWithoutPlayback()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath("silent-mulaw.wav");
        {
            QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));
            QDataStream out(&file);out.setByteOrder(QDataStream::LittleEndian);
            out.writeRawData("RIFF",4);out << quint32(36 + 8000);
            out.writeRawData("WAVEfmt ",8);
            out << quint32(16) << quint16(7) << quint16(1) << quint32(8000)
                << quint32(8000) << quint16(1) << quint16(8);
            out.writeRawData("data",4);out << quint32(8000);
            const QByteArray silence(8000, char(0xff));
            out.writeRawData(silence.constData(), silence.size());
        }
        QCOMPARE(AudioDurationProbe::waveDuration(path), 0.); // Requires multimedia metadata.
        TimelineDocument document;
        document.setSource("speech", path);
        auto player = document.findChild<QMediaPlayer *>();
        QVERIFY(player);
        QVERIFY(player->audioOutput() == nullptr);
        QTRY_COMPARE_WITH_TIMEOUT(document.track("speech").value("mediaDuration").toDouble(), 1., 10000);
        QCOMPARE(document.duration(), 1.);
        QCOMPARE(player->playbackState(), QMediaPlayer::StoppedState);
    }

    void sourcesAndOptionsAreValidated()
    {
        TimelineDocument document;
        document.setSource("image", "/tmp/frame one.png");
        QCOMPARE(document.track("image").value("source").toString(), QString("file:///tmp/frame one.png"));
        QCOMPARE(document.track("image").value("kind").toString(), QString("image"));
        document.setSource("speech", "file:///tmp/talking.mp4");
        QCOMPARE(document.track("speech").value("kind").toString(), QString("video"));
        document.setValue("speech", "audioSource", "/tmp/narration.wav");
        QCOMPARE(document.track("speech").value("audioSource").toString(), QString("file:///tmp/narration.wav"));
        document.setValue("speech", "gain", 7.);
        QCOMPARE(document.track("speech").value("gain").toDouble(), 2.);
        document.setValue("background", "color", "salmon");
        QCOMPARE(document.track("background").value("color").toString(), QString("#fa8072"));
        document.setValue("background", "color", "#80aabbcc");
        QCOMPARE(document.track("background").value("color").toString(), QString("#aabbcc"));
        document.setValue("sfx", "enabled", false);
        QVERIFY(!document.track("sfx").value("enabled").toBool());
        const auto snapshot = document.snapshot();
        document.setSource("image", "relative-path.png");
        document.setValue("background", "color", "invalid-color");
        document.setValue("speech", "gain", std::numeric_limits<double>::quiet_NaN());
        document.setValue("speech", "gain", true);
        document.setValue("image", "key", "injected");
        document.setValue("sfx", "enabled", "false");
        QCOMPARE(document.snapshot(), snapshot);
    }

    void roundTripAndCopiesRemainIndependent()
    {
        TimelineDocument original;
        original.setSource("sfx", "file:///tmp/effect.wav");
        original.setRange("sfx", 8., 12.);
        original.setValue("speech", "audioSource", "file:///tmp/voice.wav");
        original.setTransform("background", .1, .2, .6, .7);
        original.setValue("background", "color", "#334455");
        original.setValue("speech", "enabled", false);
        const auto saved = original.snapshot();
        const auto jsonBytes = QJsonDocument(saved).toJson();
        TimelineDocument duplicate;
        duplicate.restore(QJsonDocument::fromJson(jsonBytes).object());
        QCOMPARE(duplicate.snapshot(), saved);
        duplicate.setRange("sfx", 22., 3.);
        duplicate.setTransform("background", 0., 0., 1., 1.);
        QCOMPARE(original.snapshot(), saved);
        QVERIFY(duplicate.snapshot() != original.snapshot());
    }

    void malformedRestoreCannotChangeSchemaOrCreateInvalidGeometry()
    {
        TimelineDocument document;
        document.restore({
            {"tracks", QJsonArray{
                QJsonObject{{"key", "speech"}, {"name", "wrong name"}, {"start", 700.},
                            {"duration", -20.}, {"width", "invalid"}, {"height", -10.},
                            {"x", 7.}, {"gain", 9.}, {"enabled", "false"},
                            {"source", "../outside.png"}, {"kind", "unknown"}, {"extra", "ignored"}},
                QJsonObject{{"key", "speech"}, {"start", 20.}},
                QJsonObject{{"key", "rogue"}, {"start", 10.}},
                QJsonObject{{"key", "sfx"}, {"start", "invalid"}, {"duration", QJsonValue::Null}}
            }}
        });
        QCOMPARE(document.tracks().size(), 3);
        const auto image = document.track("speech");
        QCOMPARE(image.value("name").toString(), QString("数字人音频"));
        QCOMPARE(image.value("start").toDouble(), 599.9);
        QCOMPARE(image.value("width").toDouble(), 1.);
        QCOMPARE(image.value("height").toDouble(), .02);
        QCOMPARE(image.value("x").toDouble(), 2.);
        QCOMPARE(image.value("gain").toDouble(), 2.);
        QVERIFY(image.value("enabled").toBool());
        QVERIFY(image.value("source").toString().isEmpty());
        QCOMPARE(image.value("kind").toString(), QString("audio"));
        QVERIFY(!image.contains("extra"));
        QVERIFY(document.track("rogue").isEmpty());
        QCOMPARE(document.track("sfx").value("start").toDouble(), 0.);
        QCOMPARE(document.track("sfx").value("duration").toDouble(), 30.);
        document.restore({});
        QCOMPARE(document.duration(), 30.);
        QCOMPARE(document.track("image").value("start").toDouble(), 0.);
    }
};

QTEST_GUILESS_MAIN(TimelineTest)
#include "timeline_tests.moc"
