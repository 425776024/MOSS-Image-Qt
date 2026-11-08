#include "ConversationDocument.h"
#include "ConversationMediaStore.h"
#include <QtTest>
#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <QTemporaryDir>
#include <QImage>
#include <QDataStream>

class ConversationMediaTest : public QObject {
    Q_OBJECT
    static void wave(const QString &path) {
        QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));
        QDataStream out(&file);out.setByteOrder(QDataStream::LittleEndian);
        const QByteArray pcm(16000*2*4,0);
        out.writeRawData("RIFF",4);out<<quint32(36+pcm.size());out.writeRawData("WAVEfmt ",8);
        out<<quint32(16)<<quint16(1)<<quint16(1)<<quint32(16000)<<quint32(32000)<<quint16(2)<<quint16(16);
        out.writeRawData("data",4);out<<quint32(pcm.size());out.writeRawData(pcm.data(),pcm.size());
    }
    static void copy(const QList<QUrl> &urls) {
        auto mime=new QMimeData;mime->setUrls(urls);QGuiApplication::clipboard()->setMimeData(mime);
    }
private slots:
    void cleanup(){QGuiApplication::clipboard()->clear();}
    void plainTextAndRemoteLinksRemainText() {
        QTemporaryDir dir;ConversationDocument doc("work",dir.path(),{});
        QGuiApplication::clipboard()->setText("普通文字\nhttps://example.com/photo.png");
        QVERIFY(!doc.pasteMedia());QVERIFY(doc.assets().isEmpty());
        copy({QUrl("https://example.com/voice.wav")});QVERIFY(!doc.pasteMedia());QVERIFY(doc.assets().isEmpty());
    }
    void mixedFilesBecomeOwnedVideoMaterials() {
        QTemporaryDir dir;const auto image=dir.filePath("图片 with spaces.png"),audio=dir.filePath("voice.wav");
        QImage picture(32,24,QImage::Format_RGB32);picture.fill(Qt::cyan);QVERIFY(picture.save(image));wave(audio);
        const auto video=QFINDTESTDATA("../assets/showcase/avatar-demo-01.mp4");QVERIFY(!video.isEmpty());
        ConversationDocument doc("work",dir.filePath("work"),{});doc.setTask("pack");
        copy({QUrl::fromLocalFile(image),QUrl::fromLocalFile(audio),QUrl::fromLocalFile(video),QUrl::fromLocalFile(image)});
        QVERIFY(doc.pasteMedia());QCOMPARE(doc.assets().size(),3);QCOMPARE(doc.messages().size(),3);QCOMPARE(doc.selected().size(),3);
        QSet<QString> kinds;
        for(const auto &value:doc.assets()) {
            const auto row=value.toMap();kinds.insert(row["kind"].toString());const auto path=QUrl(row["url"].toString()).toLocalFile();
            QVERIFY(QFileInfo::exists(path));QVERIFY(!ConversationMediaStore::ownedDirectory(path).isEmpty());
            QVERIFY(path!=image&&path!=audio&&path!=video);
        }
        QCOMPARE(kinds,QSet<QString>({"image","audio","video"}));
        QVERIFY(QFile::remove(image));QVERIFY(QFile::remove(audio));
        ConversationDocument restored("work",dir.filePath("work"),doc.json());QCOMPARE(restored.selected().size(),3);
        for(const auto &value:restored.assets())QVERIFY(QFileInfo::exists(QUrl(value.toMap()["url"].toString()).toLocalFile()));
        QVERIFY(QFileInfo::exists(video));
    }
    void avatarPasteSelectsPortraitAndNarration() {
        QTemporaryDir dir;const auto image=dir.filePath("portrait.png"),audio=dir.filePath("voice.wav");
        QImage picture(32,24,QImage::Format_RGB32);picture.fill(Qt::yellow);QVERIFY(picture.save(image));wave(audio);
        ConversationDocument doc("work",dir.filePath("work"),{});doc.setTask("avatar");doc.setDraft("原有文案");
        copy({QUrl::fromLocalFile(image),QUrl::fromLocalFile(audio)});QVERIFY(doc.pasteMedia());
        QCOMPARE(doc.task(),QString("avatar"));QCOMPARE(doc.target()["kind"].toString(),QString("image"));QCOMPARE(doc.audio()["kind"].toString(),QString("audio"));
        QCOMPARE(doc.draft(),QString("原有文案"));QVERIFY(doc.selected().isEmpty());
    }
    void libraryNameAndSelectionSurviveReopen() {
        QTemporaryDir dir;const auto source=dir.filePath("internal-name.png");
        QImage picture(32,24,QImage::Format_RGB32);picture.fill(Qt::green);QVERIFY(picture.save(source));
        ConversationDocument doc("work",dir.filePath("work"),{});doc.setTask("avatar");
        const auto id=doc.importMedia(QUrl::fromLocalFile(source),"target","林悦 · 主播");QVERIFY(!id.isEmpty());
        QCOMPARE(doc.target()["name"].toString(),QString("林悦 · 主播"));
        ConversationDocument restored("work",dir.filePath("work"),doc.json());QCOMPARE(restored.target()["id"].toString(),id);
        QCOMPARE(restored.target()["name"].toString(),QString("林悦 · 主播"));QVERIFY(QFileInfo::exists(source));
    }
    void screenshotPasteAndPartialFailure() {
        QTemporaryDir dir;ConversationDocument doc("work",dir.filePath("work"),{});doc.setTask("edit");
        QImage picture(32,24,QImage::Format_RGB32);picture.fill(Qt::red);QGuiApplication::clipboard()->setImage(picture);
        QVERIFY(doc.pasteMedia());QVERIFY(!doc.target().isEmpty());
        const auto source=dir.filePath("valid.png");QVERIFY(picture.save(source));
        copy({QUrl::fromLocalFile(dir.filePath("missing.wav")),QUrl::fromLocalFile(source)});
        QVERIFY(doc.pasteMedia());QCOMPARE(doc.assets().size(),2);QVERIFY(!doc.error().isEmpty());
    }
};
QTEST_MAIN(ConversationMediaTest)
#include "conversation_media_tests.moc"
