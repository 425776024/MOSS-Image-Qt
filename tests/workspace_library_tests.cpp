#include "WorkspaceLibrary.h"
#include "SoundPlan.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QImage>
#include <QFile>
#include <QDir>
#include <QDataStream>

class WorkspaceLibraryTest : public QObject {
    Q_OBJECT
private slots:
    void avatarLibraryOwnsItsSourceAndRestoresAcrossRestart() {
        QTemporaryDir dir;
        const auto source = dir.filePath("portrait.png");
        QImage image(80, 120, QImage::Format_RGB32); image.fill(Qt::blue); QVERIFY(image.save(source));
        QString id, ownedPath;
        {
            WorkspaceLibrary library(dir.filePath("works"));
            QVERIFY(!library.importAvatar(QUrl::fromLocalFile(source), " ").isEmpty());
            QVERIFY(library.avatars().isEmpty());
            QVERIFY(library.importAvatar(QUrl::fromLocalFile(source), "测试形象").isEmpty());
            const auto row = library.avatars().first().toMap(); id = row["id"].toString();
            ownedPath = QUrl(row["url"].toString()).toLocalFile(); QVERIFY(ownedPath != source);
            QVERIFY(QFile::remove(source)); QCOMPARE(QImage(ownedPath), image);
            QVERIFY(library.renameAvatar(id, "我的形象").isEmpty());
            QVERIFY(library.removeAvatar(id).isEmpty()); QVERIFY(library.avatars().isEmpty());
            QVERIFY(QFile::exists(ownedPath)); QVERIFY(library.canRestoreAvatar());
        }
        WorkspaceLibrary restored(dir.filePath("works"));
        QVERIFY(restored.avatars().isEmpty()); QVERIFY(restored.restoreAvatar().isEmpty());
        QCOMPARE(restored.avatars().first().toMap()["name"].toString(), QString("我的形象"));
        QVERIFY(!restored.canRestoreAvatar()); QCOMPARE(QImage(ownedPath), image);
    }
    void voiceImportRequiresManualTranscriptAndKeepsManagedAudio() {
        QTemporaryDir dir;
        const auto source = dir.filePath("reference.wav");
        QFile audio(source); QVERIFY(audio.open(QIODevice::WriteOnly));
        QDataStream out(&audio); out.setByteOrder(QDataStream::LittleEndian);
        const quint32 bytes = 24000 * 2 * 4;
        out.writeRawData("RIFF", 4); out << quint32(36 + bytes); out.writeRawData("WAVEfmt ", 8);
        out << quint32(16) << quint16(1) << quint16(1) << quint32(24000) << quint32(48000) << quint16(2) << quint16(16);
        out.writeRawData("data", 4); out << bytes; audio.write(QByteArray(bytes, '\0')); audio.close();
        QString id, ownedPath;
        {
            SoundPlan sound; sound.initialize(dir.path());
            const auto count = sound.voices().size();
            QVERIFY(!sound.createVoice("测试声音", QUrl::fromLocalFile(source), " ").isEmpty());
            QCOMPARE(sound.voices().size(), count);
            QVERIFY(sound.createVoice("测试声音", QUrl::fromLocalFile(source), "这是手动填写的参考文案。").isEmpty());
            const auto row = sound.selectedVoice(); id = row["id"].toString(); ownedPath = row["reference"].toString();
            QCOMPARE(row["transcript"].toString(), QString("这是手动填写的参考文案。"));
            QVERIFY(ownedPath != source); QVERIFY(QFile::remove(source)); QVERIFY(QFile::exists(ownedPath));
            QVERIFY(sound.updateVoice(id, "新名字", "修改后的实际录音原文").isEmpty());
            QVERIFY(sound.removeVoice(id).isEmpty()); QVERIFY(sound.canRestoreVoice());
            QVERIFY(sound.selectedVoice()["id"].toString() != id); QVERIFY(QFile::exists(ownedPath));
        }
        SoundPlan restored; restored.initialize(dir.path()); QVERIFY(restored.restoreVoice().isEmpty());
        restored.set("voiceId", id); const auto row = restored.selectedVoice();
        QCOMPARE(row["name"].toString(), QString("新名字"));
        QCOMPARE(row["transcript"].toString(), QString("修改后的实际录音原文")); QVERIFY(!restored.canRestoreVoice());
        for (const auto &voice : restored.voices()) if (voice.toMap()["builtin"].toBool()) {
            QVERIFY(!restored.removeVoice(voice.toMap()["id"].toString()).isEmpty()); break;
        }
    }
};
QTEST_GUILESS_MAIN(WorkspaceLibraryTest)
#include "workspace_library_tests.moc"
