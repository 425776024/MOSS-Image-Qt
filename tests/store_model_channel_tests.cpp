#include <QtTest>
#include <QSettings>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QCryptographicHash>
#include <QTemporaryDir>
#include "ModelManager.h"
#include "AppleAssetDownload.h"
class StoreChannelTests: public QObject {
    Q_OBJECT
    QTemporaryDir settings;
private slots:
    void initTestCase(){QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());}
    void channelsAreIsolated(){
        QSettings().clear();QSettings().setValue("modelEndpoint","https://huggingface.co");
        ModelManager m(nullptr,"{\"groups\":[],\"files\":[]}");
        QCOMPARE(m.endpoint(),QString("apple-assets"));QVERIFY(!m.rootSelectable());
        QCOMPARE(m.imageQuant(),QString("Q4_K"));
        m.setEndpoint("https://modelscope.cn");
        QCOMPARE(QSettings().value("storeModelEndpoint").toString(),QString("apple-assets"));
        QCOMPARE(QSettings().value("modelEndpoint").toString(),QString("https://huggingface.co"));
        ModelManager restored(nullptr,"{\"groups\":[],\"files\":[]}");
        QCOMPARE(restored.endpoint(),QString("apple-assets"));
    }
    void obsoleteStoreMirrorMigratesWithoutChangingPublicChannel() {
        QSettings().clear();QSettings().setValue("modelEndpoint","https://huggingface.co");
        QSettings().setValue("storeModelEndpoint","https://hf-mirror.com");
        ModelManager manager(nullptr,"{\"groups\":[],\"files\":[]}");
        QCOMPARE(manager.endpoint(),QString("apple-assets"));
        QCOMPARE(QSettings().value("storeModelEndpoint").toString(),QString("apple-assets"));
        QCOMPARE(QSettings().value("modelEndpoint").toString(),QString("https://huggingface.co"));
        QCOMPARE(manager.sourceValues(),QStringList{"apple-assets"});
        for(const auto &source:{"auto","https://modelscope.cn","https://huggingface.co"}) {
            manager.setEndpoint(source);QCOMPARE(manager.endpoint(),QString("apple-assets"));
        }
    }
    void unconfiguredBundleFailsWithoutCallingApple(){
        AppleAssetDownload transport;QSignalSpy done(&transport,&AppleAssetDownload::completed);
        transport.start("model-test","test","/unused","/unused",10);
        QCOMPARE(done.count(),1);QVERIFY(!done.at(0).at(0).toString().isEmpty());
    }
    void storeCatalogPinsFocusedModelAndDependencies(){
        const auto assets=ModelManager::assets();
        int denoise=0,landmarker=0,sfxCount=0; qint64 sfxBytes=0;
        for(const auto &a:assets){
            if(a.group=="sfx") {++sfxCount;sfxBytes+=a.bytes;QVERIFY(!a.path.contains("medium"));}
            if(a.path=="ditto-original/denoise.onnx"){
                ++denoise;QVERIFY(a.appleOnly);QVERIFY(a.repo.isEmpty());
                QCOMPARE(a.sha256,QString("7d49421632e45970e3eb61266ffcdc6edfdd08f6630fba8d84b91b6805033798"));
            }
            if(a.path=="ditto-original/face_landmarker.task"){++landmarker;QVERIFY(a.appleOnly);}
            if(!a.resource.isEmpty()){
                QFile file(a.resource);QVERIFY(file.open(QIODevice::ReadOnly));const auto data=file.readAll();
                if(a.path=="ditto-original/manifest.json") {
                    const auto manifest=QJsonDocument::fromJson(data).object();
                    QCOMPARE(manifest["audioModel"].toObject()["trainingSteps"].toInt(),5000);
                    QCOMPARE(manifest["gpuCompatibility"].toObject()["sourceModels"].toObject()["denoise.onnx"].toObject()["sourceSha256"].toString(),QString("bb30d10ccc0ef8746c2623b558cebb8fa6e37dbc1ec0f7bdc04a7243fafa3164"));
                }
                QCOMPARE(data.size(),a.bytes);
                QCOMPARE(QString::fromLatin1(QCryptographicHash::hash(data,QCryptographicHash::Sha256).toHex()),a.sha256);
            }
        }
        QCOMPARE(denoise,1);QCOMPARE(landmarker,1);
        QCOMPARE(sfxCount,5);QCOMPARE(sfxBytes,qint64(846846464));
    }
    void appleOnlyNeverUsesMirror(){
        QStandardPaths::setTestModeEnabled(true);
        const QByteArray catalog=R"({"groups":[{"id":"isolated-test"}],"files":[{"group":"isolated-test","path":"isolated-channel-test.bin","file":"never-fetch","sha256":"test","bytes":10,"appleOnly":true}]})";
        ModelManager m(nullptr,catalog,QUrl("http://127.0.0.1:1/"));
        m.setEndpoint("https://huggingface.co");QSignalSpy done(&m,&ModelManager::completed);
        m.requestDownload("isolated-test");m.startDownload();
        QTRY_COMPARE(done.count(),1);QVERIFY(!done.at(0).at(0).toBool());
        QVERIFY(m.error().contains("App Store"));QVERIFY(!m.error().contains("镜像"));
    }
};
QTEST_GUILESS_MAIN(StoreChannelTests)
#include "store_model_channel_tests.moc"
