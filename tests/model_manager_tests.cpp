#include "ModelManager.h"
#include "WarpModelConversion.h"
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
#include <QRegularExpression>

class ModelManagerTests:public QObject {
    Q_OBJECT
    QTemporaryDir settings;
    const QByteArray payload="public-model-fixture";
    QByteArray catalog() {
        QJsonArray files,groups;
        for(const auto &id:{"image-q4","image-q8","image-shared","ditto"}) {
            groups.append(QJsonObject{{"id",id},{"title",id}});
            files.append(QJsonObject{{"group",id},{"path",QString(id)+"/model.bin"},{"repo","test/public"},
                {"modelscopeUrl","https://modelscope.cn/unused-fixture"},
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
    void slowDownloadContinuesAndResumesAfterCancel_data() {
        QTest::addColumn<bool>("sendSlowData");
        QTest::newRow("no-response")<<false;
        QTest::newRow("slow-with-restored-prefix")<<true;
    }
    void slowDownloadContinuesAndResumesAfterCancel() {
        QFETCH(bool,sendSlowData);
        QTemporaryDir root;QTcpServer server;QVERIFY(server.listen(QHostAddress::LocalHost));
        const QByteArray body((sendSlowData?16:4)*1024*1024,'p');bool resume=false;bool resumedAtOffset=false;
        connect(&server,&QTcpServer::newConnection,this,[&]{
            auto socket=server.nextPendingConnection();
            connect(socket,&QTcpSocket::readyRead,socket,[&,socket]{
                const auto request=socket->property("request").toByteArray()+socket->readAll();socket->setProperty("request",request);
                if(!request.contains("\r\n\r\n")||socket->property("handled").toBool())return;
                socket->setProperty("handled",true);
                const auto match=QRegularExpression("Range: bytes=([0-9]+)-([0-9]+)",QRegularExpression::CaseInsensitiveOption).match(QString::fromLatin1(request));
                const qint64 offset=match.captured(1).toLongLong(),end=match.captured(2).toLongLong()+1;
                if(!resume&&!sendSlowData)return;
                socket->write("HTTP/1.1 206 Partial Content\r\nContent-Range: bytes "+QByteArray::number(offset)+"-"+QByteArray::number(end-1)+"/"+QByteArray::number(body.size())+"\r\nContent-Length: "+QByteArray::number(end-offset)+"\r\n\r\n");
                if(resume) {resumedAtOffset=offset>0;socket->write(body.mid(offset,end-offset));socket->disconnectFromHost();return;}
                auto timer=new QTimer(socket);timer->setInterval(200);
                connect(timer,&QTimer::timeout,socket,[socket]{socket->write(QByteArray(512,'p'));});timer->start();
            });
            connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
        });
        QJsonObject asset{{"group","ditto"},{"path","ditto/model.bin"},{"file","file"},{"bytes",body.size()},
            {"sha256",QString::fromLatin1(QCryptographicHash::hash(body,QCryptographicHash::Sha256).toHex())}};
        const auto data=QJsonDocument(QJsonObject{{"groups",QJsonArray{QJsonObject{{"id","ditto"}}}},{"files",QJsonArray{asset}}}).toJson();
        const auto part=root.filePath("ditto/model.bin.part");
        if(sendSlowData)write(part,body.left(12*1024*1024));
        ModelManager manager(nullptr,data,QUrl(QString("http://127.0.0.1:%1/").arg(server.serverPort())));manager.setRoot(root.path());
        manager.requestDownload("ditto");QSignalSpy done(&manager,&ModelManager::completed),offered(&manager,&ModelManager::downloadRequested);
        manager.startDownload();
        // Cross the former 10-second speed floor. Neither slow progress nor an
        // initially silent connection should force a source-selection dialog.
        QTest::qWait(11500);
        QVERIFY(manager.busy());QVERIFY(!manager.paused());
        if(sendSlowData){QVERIFY(manager.downloadBytesPerSecond()>0);QVERIFY(manager.downloadBytesPerSecond()<10000);}
        else QCOMPARE(manager.downloadBytesPerSecond(),0.0);
        QVERIFY(manager.error().isEmpty());QCOMPARE(done.size(),0);QCOMPARE(offered.size(),0);
        manager.cancel();QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(),4000);
        QCOMPARE(manager.downloadBytesPerSecond(),0.0);
        QCOMPARE(done.size(),1);QVERIFY(!done[0][0].toBool());QCOMPARE(offered.size(),0);
        QVERIFY(!manager.locked());
        QVERIFY(QFileInfo::exists(part+".ranges.json"));
        if(sendSlowData)QVERIFY(QFileInfo(part).size()>12*1024*1024);
        resume=true;manager.setEndpoint("https://modelscope.cn");manager.startDownload();
        QVERIFY(!manager.paused());QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(),4000);
        QVERIFY2(manager.error().isEmpty(),qPrintable(manager.error()));QVERIFY(done.last()[0].toBool());
        QCOMPARE(read(root.filePath("ditto/model.bin")),body);QVERIFY(manager.ready("ditto"));
        QCOMPARE(resumedAtOffset,sendSlowData);QVERIFY(!QFileInfo::exists(part+".ranges.json"));
    }
    void warpConversionDownloadsAndRecoversConvertedPartial() {
        const auto source=QByteArray::fromBase64("CAo6fwotCgF4CgRncmlkEgF5IgpHcmlkU2FtcGxlKhMKBG1vZGUiCGJpbGluZWFyoAEDCi0KAXkKBGdyaWQSAXoiCkdyaWRTYW1wbGUqEwoEbW9kZSIIYmlsaW5lYXKgAQMSB2ZpeHR1cmUqFggCEAEiCAAAgD8AAABAQgZ0ZW5zb3JCBAoAEBE=");
        const auto expected=QByteArray::fromBase64("CAk6eworCgF4CgRncmlkEgF5IgpHcmlkU2FtcGxlKhEKBG1vZGUiBmxpbmVhcqABAworCgF5CgRncmlkEgF6IgpHcmlkU2FtcGxlKhEKBG1vZGUiBmxpbmVhcqABAxIHZml4dHVyZSoWCAIQASIIAACAPwAAAEBCBnRlbnNvckIECgAQFA==");
        QCOMPARE(WarpModelConversion::convert(source),expected);
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,WarpModelConversion::convert(source.left(source.size()-3)));
        QTemporaryDir root;QTcpServer server;QVERIFY(server.listen(QHostAddress::LocalHost));int requests=0;
        connect(&server,&QTcpServer::newConnection,this,[&]{auto socket=server.nextPendingConnection();
            connect(socket,&QTcpSocket::readyRead,socket,[&,socket]{
                const auto data=socket->property("request").toByteArray()+socket->readAll();socket->setProperty("request",data);
                if(!data.contains("\r\n\r\n")||socket->property("handled").toBool())return;
                socket->setProperty("handled",true);++requests;
                socket->write("HTTP/1.1 206 Partial Content\r\nContent-Range: bytes 0-"+QByteArray::number(source.size()-1)+"/"+QByteArray::number(source.size())+"\r\nContent-Length: "+QByteArray::number(source.size())+"\r\nConnection: close\r\n\r\n"+source);socket->disconnectFromHost();
            });connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
        });
        const auto hash=[](QByteArray data){return QString::fromLatin1(QCryptographicHash::hash(data,QCryptographicHash::Sha256).toHex());};
        QJsonObject asset{{"group","ditto"},{"path","ditto-original/warp.onnx"},{"file","warp"},{"bytes",expected.size()},{"sha256",hash(expected)},
            {"downloadBytes",source.size()},{"downloadSha256",hash(source)},{"conversion","warp-opset20"}};
        const auto catalog=QJsonDocument(QJsonObject{{"groups",QJsonArray{QJsonObject{{"id","ditto"}}}},{"files",QJsonArray{asset}}}).toJson();
        ModelManager m(nullptr,catalog,QUrl(QString("http://127.0.0.1:%1/").arg(server.serverPort())));m.setRoot(root.path());
        m.requestDownload("ditto");m.startDownload();QTRY_VERIFY_WITH_TIMEOUT(!m.busy(),5000);
        QVERIFY2(m.error().isEmpty(),qPrintable(m.error()));QCOMPARE(requests,1);
        const auto path=root.filePath("ditto-original/warp.onnx");QCOMPARE(read(path),expected);QVERIFY(QFile::remove(path));
        // Crash after atomic conversion but before journal removal: recover the
        // verified target instead of treating its old source ranges as corrupt.
        write(path+".part",expected);
        write(path+".part.ranges.json",QJsonDocument(QJsonObject{{"identity",hash(source)},{"total",source.size()},
            {"ranges",QJsonArray{QJsonObject{{"begin",0},{"end",source.size()},{"received",source.size()}}}}}).toJson());
        m.requestDownload("ditto");m.startDownload();QTRY_VERIFY(!m.busy());
        QVERIFY(m.error().isEmpty());QCOMPARE(requests,1);QCOMPARE(read(path),expected);
        QVERIFY(!QFileInfo::exists(path+".part.ranges.json"));
    }
    void liveWarpConversion() {
        const auto path=qEnvironmentVariable("MOSS_LIVE_WARP_PATH");if(path.isEmpty())QSKIP("Opt-in pinned Warp conversion");
        ModelAsset warp;for(const auto &a:ModelManager::assets())if(a.path=="ditto-original/warp.onnx")warp=a;
        const auto source=read(path);QCOMPARE(source.size(),warp.transferBytes());
        QCOMPARE(QString::fromLatin1(QCryptographicHash::hash(source,QCryptographicHash::Sha256).toHex()),warp.transferSha256());
        const auto converted=WarpModelConversion::convert(source);QCOMPARE(converted.size(),warp.bytes);
        QCOMPARE(QString::fromLatin1(QCryptographicHash::hash(converted,QCryptographicHash::Sha256).toHex()),warp.sha256);
    }
    void liveDittoSupportFiles_data() {
        QTest::addColumn<QString>("endpoint");
        QTest::newRow("modelscope")<<QString("https://modelscope.cn");
    }
    void liveDittoSupportFiles() {
        const auto evidence=qEnvironmentVariable("MOSS_LIVE_DITTO_SUPPORT");
        if(evidence.isEmpty())QSKIP("Opt-in pinned Ditto support download: set MOSS_LIVE_DITTO_SUPPORT");
        QFETCH(QString,endpoint);
        QFile catalogFile(QFINDTESTDATA("../config/models.json"));QVERIFY(catalogFile.open(QIODevice::ReadOnly));
        auto data=QJsonDocument::fromJson(catalogFile.readAll()).object();QJsonArray support;
        qint64 bytes=0;
        for(const auto &value:data["files"].toArray()) {
            const auto file=value.toObject();
            if(file["group"]=="ditto"&&(!file["resource"].toString().isEmpty()||file["path"].toString().contains("yunet"))) {
                support.append(file);bytes+=file["bytes"].toInteger();
            }
        }
        QCOMPARE(support.size(),4);QCOMPARE(bytes,qint64(465746));data["files"]=support;
        const auto root=evidence+"/"+QString::fromLatin1(QTest::currentDataTag());
        QVERIFY(QDir().mkpath(root));ModelManager manager(nullptr,QJsonDocument(data).toJson());
        manager.setRoot(root);manager.setEndpoint(endpoint);manager.requestDownload("ditto");
        QSignalSpy done(&manager,&ModelManager::completed);manager.startDownload();
        QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(),90000);
        QVERIFY2(manager.error().isEmpty(),qPrintable(manager.error()));QVERIFY(!manager.paused());
        QCOMPARE(done.size(),1);QVERIFY(done[0][0].toBool());QVERIFY(manager.ready("ditto"));
        for(const auto &value:support) {
            const auto file=value.toObject();const auto body=read(root+"/"+file["path"].toString());
            QCOMPARE(body.size(),file["bytes"].toInteger());
            QCOMPARE(QString::fromLatin1(QCryptographicHash::hash(body,QCryptographicHash::Sha256).toHex()),file["sha256"].toString());
        }
    }
    void publicNetworkModelsHavePinnedProviderCandidates() {
        int count=0,modelscope=0;for(const auto &a:ModelManager::assets())if(!a.repo.isEmpty()) {
            ++count;QCOMPARE(a.transferSha256().size(),64);QVERIFY(a.transferBytes()>0);
            QVERIFY(!a.revision.isEmpty());QVERIFY(!a.file.isEmpty());
            // Small's four new files have no verified ModelScope mirror.
            if(a.modelscopeUrl.isEmpty()){QCOMPARE(a.group,QString("sfx"));continue;}
            ++modelscope;
            const QUrl url(a.modelscopeUrl);QCOMPARE(url.host(),QString("modelscope.cn"));QVERIFY(a.transferBytes()>0);
        }QCOMPARE(count,20);QCOMPARE(modelscope,16);
    }
    void missingModelScopeMirrorUsesPinnedHuggingFace() {
        ModelAsset asset;asset.repo="owner/model";asset.revision=QString(40,'a');asset.file="weights.gguf";
        const QUrl upstream("https://huggingface.co/owner/model/resolve/"+asset.revision+"/weights.gguf");
        QCOMPARE(asset.downloadUrls("https://modelscope.cn"),QList<QUrl>{upstream});
        QCOMPARE(asset.downloadUrls("auto"),QList<QUrl>{upstream});
        asset.modelscopeUrl="https://modelscope.cn/api/v1/models/mirror/model/repo?Revision=pinned&FilePath=weights.gguf";
        const QUrl mirror(asset.modelscopeUrl);
        QCOMPARE(asset.downloadUrls("https://modelscope.cn"),QList<QUrl>{mirror});
        QCOMPARE(asset.downloadUrls("https://huggingface.co"),QList<QUrl>{upstream});
        const QList<QUrl> automatic={mirror,upstream};QCOMPARE(asset.downloadUrls("auto"),automatic);
        QVERIFY(asset.downloadUrls("apple-assets").isEmpty());
        asset.appleOnly=true;QVERIFY(asset.downloadUrls("https://modelscope.cn").isEmpty());
    }
    void smallCatalogHasCompletePinnedDownloadSet() {
        QStringList expected={"stable-audio-3-small-sfx-dit-0.5B-v1.0-Q4_K_M.gguf",
            "stable-audio-3-small-sfx-conditioner-v1.0-F32.gguf",
            "stable-audio-3-small-sfx-same-s-v1.0-F16.gguf",
            "t5gemma-b-b-ul2-encoder-0.3B-v1.0-Q8_0.gguf","t5gemma-b-b-ul2-v1.0-vocab.gguf"};
        int count=0;qint64 bytes=0;
        for(const auto &a:ModelManager::assets())if(a.group=="sfx") {
            ++count;bytes+=a.bytes;QVERIFY(expected.removeOne(a.file));
            QCOMPARE(a.path,"sound-effects/"+a.file);
            QVERIFY(!a.revision.isEmpty());QCOMPARE(a.sha256.size(),64);
            const auto urls=a.downloadUrls("https://modelscope.cn");QCOMPARE(urls.size(),1);
            QCOMPARE(urls.first().host(),a.modelscopeUrl.isEmpty()?QString("huggingface.co"):QString("modelscope.cn"));
        }
        QVERIFY(expected.isEmpty());QCOMPARE(count,5);QCOMPARE(bytes,qint64(846846464LL));
    }
    void catalogProviderUrlDownloadsAndVerifiesBeforeInstall() {
        QTemporaryDir root;QTcpServer server;QVERIFY(server.listen(QHostAddress::LocalHost));
        QByteArray request;
        connect(&server,&QTcpServer::newConnection,this,[&]{
            auto socket=server.nextPendingConnection();
            connect(socket,&QTcpSocket::readyRead,socket,[&,socket]{
                const auto data=socket->property("request").toByteArray()+socket->readAll();socket->setProperty("request",data);
                if(!data.contains("\r\n\r\n")||socket->property("handled").toBool())return;
                socket->setProperty("handled",true);request=data;
                socket->write("HTTP/1.1 206 Partial Content\r\nContent-Range: bytes 0-"+QByteArray::number(payload.size()-1)+"/"+QByteArray::number(payload.size())+
                    "\r\nContent-Length: "+QByteArray::number(payload.size())+"\r\nConnection: close\r\n\r\n"+payload);
                socket->disconnectFromHost();
            });connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
        });
        QJsonObject asset{{"group","sfx"},{"path","sound-effects/model.gguf"},{"file","unused"},
            {"bytes",payload.size()},{"sha256",QString::fromLatin1(QCryptographicHash::hash(payload,QCryptographicHash::Sha256).toHex())},
            {"modelscopeUrl",QString("http://127.0.0.1:%1/api/v1/models/test/repo?Revision=pinned&FilePath=medium.gguf").arg(server.serverPort())}};
        const auto data=QJsonDocument(QJsonObject{{"groups",QJsonArray{QJsonObject{{"id","sfx"}}}},{"files",QJsonArray{asset}}}).toJson();
        ModelManager manager(nullptr,data);manager.setRoot(root.path());manager.setEndpoint("https://modelscope.cn");manager.acceptSfxTerms();
        manager.requestDownload("sfx");manager.startDownload();QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(),5000);
        QVERIFY2(manager.error().isEmpty(),qPrintable(manager.error()));
        QVERIFY(request.startsWith("GET /api/v1/models/test/repo?Revision=pinned&FilePath=medium.gguf "));
        QCOMPARE(read(root.filePath("sound-effects/model.gguf")),payload);QVERIFY(manager.ready("sfx"));
    }
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
    void dittoSupportRemainderDoesNotReplaceFullResourceSize() {
        QTemporaryDir root;ModelManager manager;manager.setRoot(root.path());qint64 total=0;
        for(const auto &asset:ModelManager::assets())if(asset.group=="ditto") {
            total+=asset.bytes;
            if(!asset.resource.isEmpty()||asset.path.contains("yunet"))continue;
            // Sparse placeholders reproduce the installed-size check without downloading 2 GB.
            const auto path=root.filePath(asset.path);QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
            QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));QVERIFY(file.resize(asset.bytes));
        }
        QVariantMap ditto;
        for(const auto &value:manager.resources())if(value.toMap()["id"]=="ditto")ditto=value.toMap();
        QCOMPARE(ditto["totalBytes"].toLongLong(),total);QCOMPARE(total,qint64(2303667175LL));
        QCOMPARE(ditto["missingBytes"].toLongLong(),qint64(465746));
        QVERIFY(ditto["totalSize"].toString()!=ditto["size"].toString());
        manager.requestDownload("ditto");QCOMPARE(manager.requestInfo()["size"],ditto["size"]);
    }
    void multiSelectionKeepsSoundTermsExplicitAndDeduplicatesImageFiles() {
        QTemporaryDir root;ModelManager manager(nullptr,catalog().replace("ditto","sfx"));manager.setRoot(root.path());
        manager.setRequested({"image","image-q4","sfx"});QVERIFY(manager.sfxTermsRequired());
        manager.startDownload();QVERIFY(!manager.busy());QVERIFY(!manager.error().isEmpty());
        manager.setRequested({"image"});QVERIFY(!manager.sfxTermsRequired());
        manager.setRequested({});QVERIFY(manager.requested().isEmpty());manager.startDownload();QVERIFY(!manager.busy());
    }
    void communityCatalogKeepsOfficialDenoiser(){
        bool found=false;
        for(const auto &a:ModelManager::assets()){
            QVERIFY(!a.appleOnly);
            QVERIFY(a.path!="ditto-original/face_landmarker.task");
            if(a.path=="ditto-original/denoise.onnx"){
                found=true;QCOMPARE(a.sha256,QString("ea6664518746a1c2e13db496ffe97216da715742be83e069a02cc86873c6dcfc"));
            }
        }
        QVERIFY(found);
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
    void publicChannelUsesModelScopeWithPinnedFallback() {
        ModelManager first(nullptr,catalog());
        QCOMPARE(first.endpoint(),QString("https://modelscope.cn"));
        QCOMPARE(first.sourceValues(),QStringList{"https://modelscope.cn"});
        QCOMPARE(first.sourceNames(),QStringList{"ModelScope · modelscope.cn"});
        QCOMPARE(first.imageQuant(),QString("Q4_K"));
        QVERIFY(!first.sourceValues().contains("apple-assets"));
        first.setEndpoint("apple-assets");
        QCOMPARE(first.endpoint(),QString("https://modelscope.cn"));
        first.setEndpoint("https://huggingface.co");
        QCOMPARE(first.endpoint(),QString("https://modelscope.cn"));
        first.setEndpoint("https://hf-mirror.com");
        QCOMPARE(first.endpoint(),QString("https://modelscope.cn"));
        first.setImageQuant("Q8_0");
        ModelManager restored(nullptr,catalog());
        QCOMPARE(restored.endpoint(),QString("https://modelscope.cn"));
        QCOMPARE(restored.imageQuant(),QString("Q8_0"));
    }
    void oldDownloadSettingsMigrateToModelScope() {
        for(const auto old:{"auto","https://hf-mirror.com","https://huggingface.co","https://untrusted.invalid"}) {
            QSettings().setValue("modelEndpoint",old);
            ModelManager manager(nullptr,catalog());
            QCOMPARE(manager.endpoint(),QString("https://modelscope.cn"));
            QCOMPARE(QSettings().value("modelEndpoint").toString(),manager.endpoint());
        }
    }
    void missingAllProviderUrlsFailsWithoutNetwork() {
        QTemporaryDir root;ModelManager manager(nullptr,catalog().replace("modelscopeUrl","omittedModelscopeUrl").replace("\"repo\"","\"omittedRepo\""));manager.setRoot(root.path());
        manager.requestDownload("ditto");QSignalSpy done(&manager,&ModelManager::completed);
        manager.startDownload();QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,3000);
        QVERIFY(!done[0][0].toBool());QVERIFY(manager.error().contains("下载地址缺失"));
        QVERIFY(!manager.ready("ditto"));
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
        QVERIFY2(m.error().isEmpty(),qPrintable(m.error()));QVERIFY2(request.toLower().contains("range: bytes=5-"),request.constData());QCOMPARE(read(root.filePath("ditto/model.bin")),payload);QVERIFY(!QFileInfo::exists(root.filePath("ditto/model.bin.part")));
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
    void multipleFilesAreDownloadedConcurrently() {
        QTemporaryDir root;QTcpServer server;QVERIFY(server.listen(QHostAddress::LocalHost));
        QList<QTcpSocket *> waiting;int requests=0;
        connect(&server,&QTcpServer::newConnection,this,[&]{auto socket=server.nextPendingConnection();
            connect(socket,&QTcpSocket::readyRead,socket,[&,socket]{
                socket->readAll();if(socket->property("handled").toBool())return;socket->setProperty("handled",true);
                ++requests;waiting<<socket;
                // Neither file can finish unless the scheduler starts both.
                if(waiting.size()==2)for(auto s:waiting){
                    s->write("HTTP/1.1 206 Partial Content\r\nContent-Range: bytes 0-"+QByteArray::number(payload.size()-1)+"/"+QByteArray::number(payload.size())+"\r\nContent-Length: "+QByteArray::number(payload.size())+"\r\nConnection: close\r\n\r\n"+payload);s->disconnectFromHost();
                }
            });connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
        });
        ModelManager m(nullptr,catalog(),QUrl(QString("http://127.0.0.1:%1/").arg(server.serverPort())));m.setRoot(root.path());
        m.requestDownload("image");m.startDownload();QTRY_VERIFY_WITH_TIMEOUT(!m.busy(),5000);
        QVERIFY2(m.error().isEmpty(),qPrintable(m.error()));QCOMPARE(requests,2);QVERIFY(m.ready("image"));
    }
    void cancelParallelQueueReleasesLockAndKeepsPartials() {
        QTemporaryDir root;QTcpServer server;QVERIFY(server.listen(QHostAddress::LocalHost));int requests=0;
        connect(&server,&QTcpServer::newConnection,this,[&]{auto socket=server.nextPendingConnection();
            connect(socket,&QTcpSocket::readyRead,socket,[&,socket]{socket->readAll();if(socket->property("handled").toBool())return;socket->setProperty("handled",true);++requests;
                socket->write("HTTP/1.1 206 Partial Content\r\nContent-Range: bytes 0-"+QByteArray::number(payload.size()-1)+"/"+QByteArray::number(payload.size())+"\r\nContent-Length: "+QByteArray::number(payload.size())+"\r\n\r\n"+payload.left(5));
            });connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
        });
        ModelManager m(nullptr,catalog(),QUrl(QString("http://127.0.0.1:%1/").arg(server.serverPort())));m.setRoot(root.path());QSignalSpy done(&m,&ModelManager::completed);
        m.requestDownload("image");m.startDownload();QTRY_COMPARE(requests,2);QTRY_VERIFY(m.progress()>0);m.cancel();QTRY_COMPARE(done.size(),1);
        QVERIFY(!m.busy());QVERIFY(!m.ready("image"));QVERIFY(m.partialBytes()>0);QVERIFY(!QFileInfo::exists(root.filePath(".models.lock")));
        m.cleanPartials();QCOMPARE(m.partialBytes(),0.);QVERIFY(!QFileInfo::exists(root.filePath("image-q4/model.bin.part.ranges.json")));
    }
    void officialCatalogIsPinnedAndPrivatePathsAreAbsent() {
        const auto files=ModelManager::assets();QVERIFY(!files.isEmpty());
        for(const auto &f:files){QVERIFY(f.bytes>0);QCOMPARE(f.sha256.size(),64);QVERIFY(!f.path.startsWith("ditto/"));QVERIFY(!f.path.contains("5000"));if(f.resource.isEmpty())QCOMPARE(f.revision.size(),40);}
    }
    void embeddedResourcesMatchPinnedBytes() {
        int count=0;
        for(const auto &asset:ModelManager::assets())if(!asset.resource.isEmpty()) {
            ++count;const auto body=read(asset.resource);
            QCOMPARE(body.size(),asset.bytes);
            QCOMPARE(QString::fromLatin1(QCryptographicHash::hash(body,QCryptographicHash::Sha256).toHex()),asset.sha256);
        }
        QCOMPARE(count,3);
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
