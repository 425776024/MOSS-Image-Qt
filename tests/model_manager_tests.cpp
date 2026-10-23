#include "ModelManager.h"
#include "TestFileSystem.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QSettings>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonArray>
#include <QJsonDocument>
#include <QCryptographicHash>

class ModelManagerTests:public QObject {
    Q_OBJECT
    QTemporaryDir settings;
    const QByteArray payload="public-model-fixture";
    QByteArray catalog() {
        QJsonArray files,groups;
        for(const auto &id:{"image-q4","image-q8","image-shared","ditto"}) {
            groups.append(QJsonObject{{"id",id},{"title",id}});
            files.append(QJsonObject{{"group",id},{"path",QString(id)+"/model.bin"},{"repo","test/public"},
                {"revision",QString(40,'a')},{"file",QString(id)}, {"bytes",payload.size()},
                {"sha256",QString::fromLatin1(QCryptographicHash::hash(payload,QCryptographicHash::Sha256).toHex())}});
        }
        return QJsonDocument(QJsonObject{{"groups",groups},{"files",files}}).toJson();
    }
    void write(QString path,QByteArray data) {
        QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));QFile f(path);QVERIFY(f.open(QIODevice::WriteOnly));QCOMPARE(f.write(data),data.size());
    }
    QByteArray read(QString path) {QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};return f.readAll();}
private slots:
    void smallestImageModelIsDefaultAndExplicitChoicePersists() {
        ModelManager first(nullptr,catalog());QCOMPARE(first.imageQuant(),QString("Q4_K"));
        first.setImageQuant("Q8_0");ModelManager restored(nullptr,catalog());QCOMPARE(restored.imageQuant(),QString("Q8_0"));
        QSettings().setValue("imageQuant","invalid");ModelManager invalid(nullptr,catalog());QCOMPARE(invalid.imageQuant(),QString("Q4_K"));
    }
    void firstLaunchOffersMultipleResourcesOnceWithoutDownloading() {
        QTemporaryDir root;ModelManager manager(nullptr,catalog());manager.setRoot(root.path());
        QSignalSpy offered(&manager,&ModelManager::downloadRequested);
        manager.showInitialSetup();QCOMPARE(offered.count(),1);QVERIFY(manager.initialSetup());
        QCOMPARE(manager.requested(),QStringList{"ditto"});QVERIFY(!manager.busy());
        QCOMPARE(manager.resources().size(),2);
        manager.setRequested({"image","ditto","image","invalid"});
        QCOMPARE(manager.requested(),(QStringList{"image","ditto"}));
        manager.dismissRequest();QVERIFY(!manager.initialSetup());QVERIFY(manager.requested().isEmpty());
        manager.showInitialSetup();QCOMPARE(offered.count(),1);
        ModelManager reopened(nullptr,catalog());reopened.setRoot(root.path());
        QSignalSpy next(&reopened,&ModelManager::downloadRequested);reopened.showInitialSetup();
        QCOMPARE(next.count(),0);QVERIFY(!reopened.busy());
        reopened.requestDownload("image");QCOMPARE(next.count(),1);
    }
    void readyInstallationSkipsSetupAndResourceSizesRespectImageQuality() {
        QTemporaryDir root;ModelManager manager(nullptr,catalog());manager.setRoot(root.path());manager.setImageQuant("Q4_K");
        for(const auto &id:{"image-q4","image-shared","ditto"})write(root.filePath(QString(id)+"/model.bin"),payload);
        QSignalSpy offered(&manager,&ModelManager::downloadRequested);manager.showInitialSetup();
        QCOMPARE(offered.count(),0);QVERIFY(!manager.initialSetup());
        QCOMPARE(manager.resources().first().toMap()["missingBytes"].toLongLong(),0);
        manager.setImageQuant("Q8_0");
        QCOMPARE(manager.resources().first().toMap()["missingBytes"].toLongLong(),payload.size());
        QCOMPARE(manager.resources().first().toMap()["totalBytes"].toLongLong(),payload.size()*2);
    }
    void multiSelectionKeepsSoundTermsExplicitAndDeduplicatesImageFiles() {
        QTemporaryDir root;ModelManager manager(nullptr,catalog().replace("ditto","sfx"));manager.setRoot(root.path());
        manager.setRequested({"image","image-q4","sfx"});QVERIFY(manager.sfxTermsRequired());
        manager.startDownload();QVERIFY(!manager.busy());QVERIFY(!manager.error().isEmpty());
        manager.setRequested({"image"});QVERIFY(!manager.sfxTermsRequired());
        manager.setRequested({});QVERIFY(manager.requested().isEmpty());manager.startDownload();QVERIFY(!manager.busy());
    }
    void initTestCase() {QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());QCoreApplication::setOrganizationName("ModelTest");QCoreApplication::setApplicationName("ModelTest");}
    void init(){QSettings().clear();qunsetenv("MOSS_MODEL_ROOT");qunsetenv("MOSS_IMAGE_QUANT");}
    void soundTermsGateDownloadAndInstalledModelUse() {
        QTemporaryDir models;
        ModelManager manager(nullptr,catalog().replace("ditto","sfx"));
        manager.setRoot(models.path());
        write(models.path()+"/sfx/model.bin",payload);
        QVERIFY(manager.ready("sfx"));
        QSignalSpy requested(&manager,&ModelManager::downloadRequested);
        QVERIFY(!manager.ensure({"sfx"}));
        QCOMPARE(requested.count(),1);
        QVERIFY(manager.sfxTermsRequired());
        manager.startDownload();
        QVERIFY(!manager.busy());
        QVERIFY(!manager.error().isEmpty());
        manager.acceptSfxTerms();
        QVERIFY(manager.sfxTermsAccepted());
        QVERIFY(!manager.sfxTermsRequired());
        QVERIFY(manager.ensure({"sfx"}));
        ModelManager restored(nullptr,catalog().replace("ditto","sfx"));
        QVERIFY(restored.sfxTermsAccepted());
        QSettings().setValue("sfxModelTermsVersion","obsolete");
        QVERIFY(!manager.sfxTermsAccepted());
    }
    void publicChannelDefaultsToMirrorAndRejectsApple() {
        ModelManager first(nullptr,catalog());
        QCOMPARE(first.endpoint(),QString("https://hf-mirror.com"));
        QVERIFY(!first.sourceValues().contains("apple-assets"));
        first.setEndpoint("apple-assets");
        QCOMPARE(first.endpoint(),QString("https://hf-mirror.com"));
        first.setEndpoint("https://huggingface.co");
        ModelManager restored(nullptr,catalog());
        QCOMPARE(restored.endpoint(),QString("https://huggingface.co"));
        QSettings().setValue("modelEndpoint","https://untrusted.invalid");
        ModelManager invalid(nullptr,catalog());
        QCOMPARE(invalid.endpoint(),QString("https://hf-mirror.com"));
    }
    void missingModelsAskWithoutCreatingFiles() {
        QTemporaryDir root;ModelManager m(nullptr,catalog());m.setRoot(root.path());QSignalSpy prompt(&m,&ModelManager::downloadRequested);
        QVERIFY(!m.ensure({"image","ditto"}));QCOMPARE(prompt.size(),1);QVERIFY(!m.busy());QVERIFY(QDir(root.path()).entryList(QDir::Files).isEmpty());
        m.setImageQuant("Q4_K");write(root.filePath("image-q4/model.bin"),payload);write(root.filePath("image-shared/model.bin"),payload);
        QVERIFY(m.ready("image"));QVERIFY(!m.ready("image-q8"));write(root.filePath("ditto/manifest.json"),"private");QVERIFY(!m.ready("ditto"));
    }
    void deletionPreservesSharedPrivateAndUnknownFiles() {
        QTemporaryDir root;ModelManager m(nullptr,catalog());m.setRoot(root.path());
        for(const auto &id:{"image-q4","image-q8","image-shared","ditto"})write(root.filePath(QString(id)+"/model.bin"),payload);
        write(root.filePath("ditto/step-05000.pth"),"private");write(root.filePath("image-q4/notes.txt"),"keep");
        m.removeGroup("image-q4");QVERIFY(!m.ready("image-q4"));QVERIFY(m.ready("image-q8"));QVERIFY(m.ready("image-shared"));
        m.removeGroup("ditto");QCOMPARE(read(root.filePath("ditto/step-05000.pth")),QByteArray("private"));QVERIFY(QFileInfo::exists(root.filePath("image-q4/notes.txt")));
        write(root.filePath("image-q8/model.bin.part"),"part");write(root.filePath("unknown.part"),"keep");QCOMPARE(m.partialBytes(),4.);m.cleanPartials();QCOMPARE(m.partialBytes(),0.);QVERIFY(m.ready("image-q8"));QVERIFY(QFileInfo::exists(root.filePath("unknown.part")));
    }
    void symlinksAndBusyOperationsAreRejected() {
        QTemporaryDir root,other;ModelManager m(nullptr,catalog());m.setRoot(root.path());write(other.filePath("model.bin"),payload);
        QVERIFY(createTestSymlink(other.path(),root.filePath("ditto")));m.removeGroup("ditto");QVERIFY(!m.error().isEmpty());QVERIFY(QFileInfo::exists(other.filePath("model.bin")));QVERIFY(!m.ready("ditto"));
        write(root.filePath("image-q4/model.bin"),payload);m.setInferenceBusy(true);m.removeGroup("image-q4");QVERIFY(m.ready("image-q4"));
        m.requestDownload("image");QVERIFY(m.requested().isEmpty());
    }
    void verifyDetectsSameSizeCorruptionAndCanCancel() {
        QTemporaryDir root;ModelManager m(nullptr,catalog());m.setRoot(root.path());write(root.filePath("ditto/model.bin"),QByteArray(payload.size(),'x'));
        m.verifyGroup("ditto");QTRY_VERIFY(!m.busy());QVERIFY(m.error().contains("SHA-256"));
        write(root.filePath("ditto/model.bin"),payload);m.verifyGroup("ditto");m.cancel();QTRY_VERIFY(!m.busy());QVERIFY(m.status().contains("取消"));QVERIFY(m.ready("ditto"));
    }
    void resumedAndIgnoredRanges_data(){QTest::addColumn<bool>("ignoreRange");QTest::newRow("range")<<false;QTest::newRow("range-ignored")<<true;}
    void resumedAndIgnoredRanges() {
        QFETCH(bool,ignoreRange);QTemporaryDir root;QTcpServer server;QVERIFY(server.listen(QHostAddress::LocalHost));QByteArray request;
        connect(&server,&QTcpServer::newConnection,this,[&]{auto socket=server.nextPendingConnection();connect(socket,&QTcpSocket::readyRead,socket,[&,socket]{
            request+=socket->readAll();if(!request.contains("\r\n\r\n"))return;
            const auto body=ignoreRange?payload:payload.mid(5);QByteArray head=ignoreRange?QByteArray("HTTP/1.1 200 OK\r\n"):"HTTP/1.1 206 Partial Content\r\nContent-Range: bytes 5-"+QByteArray::number(payload.size()-1)+"/"+QByteArray::number(payload.size())+"\r\n";
            socket->write(head+"Content-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body);socket->disconnectFromHost();});connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);});
        ModelManager m(nullptr,catalog(),QUrl(QString("http://127.0.0.1:%1/").arg(server.serverPort())));m.setRoot(root.path());
        write(root.filePath("ditto/model.bin.part"),payload.left(5));m.requestDownload("ditto");m.startDownload();QTRY_VERIFY_WITH_TIMEOUT(!m.busy(),5000);
        QVERIFY2(m.error().isEmpty(),qPrintable(m.error()));QVERIFY(request.contains("Range: bytes=5-"));QCOMPARE(read(root.filePath("ditto/model.bin")),payload);QVERIFY(!QFileInfo::exists(root.filePath("ditto/model.bin.part")));
    }
    void badHashNeverInstalls() {
        QTemporaryDir root;QTcpServer server;QVERIFY(server.listen(QHostAddress::LocalHost));
        connect(&server,&QTcpServer::newConnection,this,[&]{auto socket=server.nextPendingConnection();connect(socket,&QTcpSocket::readyRead,socket,[&,socket]{socket->readAll();socket->write("HTTP/1.1 200 OK\r\nContent-Length: "+QByteArray::number(payload.size())+"\r\nConnection: close\r\n\r\n"+QByteArray(payload.size(),'x'));socket->disconnectFromHost();});connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);});
        ModelManager m(nullptr,catalog(),QUrl(QString("http://127.0.0.1:%1/").arg(server.serverPort())));m.setRoot(root.path());m.requestDownload("ditto");m.startDownload();QTRY_VERIFY_WITH_TIMEOUT(!m.busy(),5000);
        QVERIFY(m.error().contains("SHA-256"));QVERIFY(!m.ready("ditto"));QVERIFY(!QFileInfo::exists(root.filePath("ditto/model.bin.part")));
    }
    void completePartialInstallsWithoutNetwork() {
        QTemporaryDir root;ModelManager m(nullptr,catalog());m.setRoot(root.path());write(root.filePath("ditto/model.bin.part"),payload);
        m.requestDownload("ditto");m.startDownload();QTRY_VERIFY(!m.busy());QVERIFY(m.error().isEmpty());QCOMPARE(read(root.filePath("ditto/model.bin")),payload);
    }
    void completedDownloadRepairsExistingModel_data() {
        QTest::addColumn<bool>("sameSize");QTest::addColumn<bool>("unicode");
        QTest::newRow("truncated")<<false<<false;
        QTest::newRow("same-size-corruption")<<true<<false;
        QTest::newRow("unicode-truncated")<<false<<true;
        QTest::newRow("unicode-same-size")<<true<<true;
    }
    void completedDownloadRepairsExistingModel() {
        QFETCH(bool,sameSize);QFETCH(bool,unicode);QTemporaryDir root;
        const auto relative=unicode?QString::fromUtf8("数字人/模型.bin"):QString("ditto/model.bin");
        const auto base=root.path()+(unicode?QString::fromUtf8("/下载资源-🦊"):QString());
        ModelManager manager(nullptr,catalog().replace("ditto/model.bin",relative.toUtf8()));manager.setRoot(base);
        const auto destination=base+"/"+relative;
        write(destination,sameSize?QByteArray(payload.size(),'x'):QByteArray("old"));
        write(destination+".part",payload);
        QSignalSpy completed(&manager,&ModelManager::completed);
        manager.requestDownload("ditto");manager.startDownload();QTRY_VERIFY(!manager.busy());
        QVERIFY2(manager.error().isEmpty(),qPrintable(manager.error()));
        QCOMPARE(completed.size(),1);QVERIFY(completed.first().first().toBool());
        QCOMPARE(read(destination),payload);QVERIFY(!QFileInfo::exists(destination+".part"));
        QVERIFY(manager.ready("ditto"));
    }
    void failedInstallationPreservesDownloadForRetry() {
        QTemporaryDir root;ModelManager manager(nullptr,catalog());manager.setRoot(root.path());
        const auto destination=root.filePath("ditto/model.bin");
        QVERIFY(QDir().mkpath(destination));write(destination+"/keep.txt","keep");write(destination+".part",payload);
        manager.requestDownload("ditto");manager.startDownload();QTRY_VERIFY(!manager.busy());
        QVERIFY(manager.error().contains("无法安装模型文件"));QCOMPARE(read(destination+".part"),payload);
        QCOMPARE(read(destination+"/keep.txt"),QByteArray("keep"));
        QVERIFY(QFile::remove(destination+"/keep.txt"));QVERIFY(QDir().rmdir(destination));
        manager.startDownload();QTRY_VERIFY(!manager.busy());
        QVERIFY2(manager.error().isEmpty(),qPrintable(manager.error()));QCOMPARE(read(destination),payload);
        QVERIFY(!QFileInfo::exists(destination+".part"));
    }
    void networkFailureIncludesContextAndPreservesPartial() {
        QTemporaryDir root;QTcpServer server;QVERIFY(server.listen(QHostAddress::LocalHost));
        connect(&server,&QTcpServer::newConnection,this,[&]{auto socket=server.nextPendingConnection();connect(socket,&QTcpSocket::readyRead,socket,[socket]{
            socket->readAll();socket->write("HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");socket->disconnectFromHost();
        });connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);});
        ModelManager manager(nullptr,catalog(),QUrl(QString("http://127.0.0.1:%1/").arg(server.serverPort())));manager.setRoot(root.path());
        write(root.filePath("ditto/model.bin.part"),payload.left(5));manager.requestDownload("ditto");manager.startDownload();QTRY_VERIFY(!manager.busy());
        QVERIFY(manager.error().contains("HTTP 503"));QVERIFY(manager.error().contains("ditto/model.bin"));
        QVERIFY(manager.error().contains(manager.endpoint()));QCOMPARE(read(root.filePath("ditto/model.bin.part")),payload.left(5));
        QVERIFY(!QFileInfo::exists(root.filePath("ditto/model.bin")));
    }
    void officialCatalogIsPinnedAndPrivatePathsAreAbsent() {
        const auto files=ModelManager::assets();QVERIFY(!files.isEmpty());
        for(const auto &f:files){QVERIFY(f.bytes>0);QCOMPARE(f.sha256.size(),64);QVERIFY(!f.path.startsWith("ditto/"));QVERIFY(!f.path.contains("5000"));if(f.resource.isEmpty())QCOMPARE(f.revision.size(),40);}
    }
    void legacyVoiceFilesDoNotSatisfyMingDownload() {
        QTemporaryDir root;ModelManager m;m.setRoot(root.path());
        for(const auto &name:{"encoder.onnx","decoder.onnx","language-model.onnx","tokenizer.json","zh_tn_tagger.fst","zh_tn_verbalizer.fst"})
            write(root.filePath("voice/"+QString(name)),payload);
        QVERIFY(!m.ready("voice"));m.requestDownload("voice");
        QCOMPARE(m.requested().size(),1);
        QList<ModelAsset> voice;
        for(const auto &asset:ModelManager::assets())if(asset.group=="voice")voice.append(asset);
        QCOMPARE(voice.size(),1);
        QCOMPARE(voice.front().path,QString("voice/ming-3.0-tts-0.5b-q8_0.gguf"));
        QCOMPARE(voice.front().bytes,qint64(1742280896));
        // Removing the current model leaves historical user files untouched.
        m.removeGroup("voice");QVERIFY(QFileInfo::exists(root.filePath("voice/encoder.onnx")));
    }
    void publicFaceModelsExcludeResearchOnlyDetector() {
        bool yunet=false;
        for(const auto &f:ModelManager::assets()) {
            QVERIFY(f.path!="ditto-original/detector.onnx");
            QVERIFY(f.path!="ditto-original/landmark106.onnx");
            if(f.path=="ditto-original/face_detection_yunet_2023mar.onnx") {
                yunet=true;
                QCOMPARE(f.repo,QString("opencv/opencv_zoo"));
                QCOMPARE(f.sha256,QString("8f2383e4dd3cfbb4553ea8718107fc0423210dc964f9f4280604804ed2552fa4"));
            }
        }
        QVERIFY(yunet);
    }
};
QTEST_GUILESS_MAIN(ModelManagerTests)
#include "model_manager_tests.moc"
