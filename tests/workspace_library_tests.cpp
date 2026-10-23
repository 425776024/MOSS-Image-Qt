#include "WorkspaceLibrary.h"
#include "SoundPlan.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QImage>
#include <QFile>
#include <QDir>
#include <QDataStream>
#include <QSettings>
#include <QCryptographicHash>

class WorkspaceLibraryTest : public QObject {
    Q_OBJECT
    QTemporaryDir m_settings;
private slots:
    void builtinPortraitsAreReadableAndRemainSeparateFromUserAvatars() {
        QTemporaryDir root;WorkspaceLibrary library(root.path());
        const auto portraits=library.builtinAvatars();QCOMPARE(portraits.size(),17);QSet<QString> ids;
        for(const auto &value:portraits){
            const auto row=value.toMap();const auto id=row["id"].toString();QVERIFY(!ids.contains(id));ids.insert(id);
            QVERIFY(row["builtin"].toBool());const auto path=QUrl(row["url"].toString()).toLocalFile();
            QVERIFY(!QImage(path).isNull());QFile file(path);QVERIFY(file.open(QIODevice::ReadOnly));
            QCOMPARE(QString::fromLatin1(QCryptographicHash::hash(file.readAll(),QCryptographicHash::Sha256).toHex()),row["sha256"].toString());
            QVERIFY(!library.removeAvatar(id).isEmpty());QVERIFY(!library.renameAvatar(id,"changed").isEmpty());
        }
        QVERIFY(library.avatars().isEmpty());QCOMPARE(library.builtinAvatars().size(),17);
        library.setSessionLanguage("en");QVERIFY(library.builtinAvatars().first().toMap()["name"].toString().startsWith("Lin Yue"));
    }
    void quickStartCompletionSurvivesRestart() {
        QTemporaryDir root;QSettings().remove("home/quickStartVersion");
        WorkspaceLibrary first(root.path());QVERIFY(first.quickStartPending());
        QSignalSpy changed(&first,&WorkspaceLibrary::preferencesChanged);
        first.completeQuickStart();QVERIFY(!first.quickStartPending());QCOMPARE(changed.count(),1);
        WorkspaceLibrary reopened(root.path());QVERIFY(!reopened.quickStartPending());
    }
    void initTestCase() {
        QVERIFY(m_settings.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,m_settings.path());
    }
    void systemLanguageResolution() {
        QCOMPARE(WorkspaceLibrary::resolveLanguage({"zh-Hans-CN", "en-US"}), QString("zh"));
        QCOMPARE(WorkspaceLibrary::resolveLanguage({"en-GB", "zh-CN"}), QString("en"));
        QCOMPARE(WorkspaceLibrary::resolveLanguage({"zh_TW"}), QString("zh"));
        QCOMPARE(WorkspaceLibrary::resolveLanguage({"fr-FR"}), QString("en"));
        QCOMPARE(WorkspaceLibrary::resolveLanguage({}), QString("en"));
    }
    void languagePreferenceAndSessionOverride() {
        QTemporaryDir directory;
        WorkspaceLibrary library(directory.path());
        QCOMPARE(library.languagePreference(),QString("system"));
        library.setLanguage("en");
        QCOMPARE(library.appName(),QString("XAvatar"));
        library.setSessionLanguage("zh");
        QCOMPARE(library.appName(),QString::fromUtf8("新身数字人"));
        QCOMPARE(QSettings().value("home/language").toString(),QString("en"));
        WorkspaceLibrary restored(directory.path());
        QCOMPARE(restored.language(),QString("en"));
        restored.setLanguage("system");
        WorkspaceLibrary automatic(directory.path());
        QCOMPARE(automatic.languagePreference(),QString("system"));
        automatic.setLanguage("invalid");
        QCOMPARE(automatic.languagePreference(),QString("system"));
    }
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
    void generatedAvatarOwnsImageAndPersistsRecipe(){
        QTemporaryDir root;const auto source=root.filePath("generated.png");QImage image(16,24,QImage::Format_RGB32);image.fill(Qt::blue);QVERIFY(image.save(source));
        WorkspaceLibrary library(root.path());QVERIFY(library.importAvatar(QUrl::fromLocalFile(source),"New avatar",{{"engine","MOSS-Image"},{"prompt","A fictional person"}}).isEmpty());
        const auto row=library.avatars().first().toMap();QVERIFY(row["synthetic"].toBool());const auto owned=QUrl(row["url"].toString()).toLocalFile();QVERIFY(owned!=source);
        QFile::remove(source);QVERIFY(QFileInfo::exists(owned));WorkspaceLibrary reopened(root.path());
        QCOMPARE(reopened.avatars().first().toMap()["generationRecipe"].toMap()["prompt"].toString(),QString("A fictional person"));
    }
    void speechStylesInvalidateAudioAndRoundTrip(){
        SoundPlan sound;QCOMPARE(sound.settings()["emotion"].toString(),QString("中性"));
        sound.setResult("speechResult","old.wav");sound.set("emotion","高兴");
        QVERIFY(sound.settings()["speechResult"].toString().isEmpty());
        sound.set("dialect","cantonese");QCOMPARE(sound.settings()["emotion"].toString(),QString("中性"));
        SoundPlan restored;restored.restore(sound.json());QCOMPARE(restored.settings()["dialect"].toString(),QString("cantonese"));
        restored.set("emotion","悲伤");QCOMPARE(restored.settings()["dialect"].toString(),QString("standard"));
        restored.set("emotion","unsupported");QCOMPARE(restored.settings()["emotion"].toString(),QString("悲伤"));
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
