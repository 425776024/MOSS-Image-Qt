#include <QtTest>
#include <QSettings>
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
        QCOMPARE(QSettings().value("storeModelEndpoint").toString(),QString("https://modelscope.cn"));
        QCOMPARE(QSettings().value("modelEndpoint").toString(),QString("https://huggingface.co"));
        ModelManager restored(nullptr,"{\"groups\":[],\"files\":[]}");
        QCOMPARE(restored.endpoint(),QString("https://modelscope.cn"));
    }
    void oldStoreSourcesMigrateWithoutChangingPublicSettings() {
        for(const auto old:{"auto","https://hf-mirror.com","https://huggingface.co"}) {
            QSettings().clear();QSettings().setValue("modelEndpoint","public-setting-sentinel");
            QSettings().setValue("storeModelEndpoint",old);
            ModelManager manager(nullptr,"{\"groups\":[],\"files\":[]}");
            QCOMPARE(manager.endpoint(),QString("https://modelscope.cn"));
            QCOMPARE(QSettings().value("storeModelEndpoint").toString(),manager.endpoint());
            QCOMPARE(QSettings().value("modelEndpoint").toString(),QString("public-setting-sentinel"));
        }
    }
    void unconfiguredBundleFailsWithoutCallingApple(){
        AppleAssetDownload transport;QSignalSpy done(&transport,&AppleAssetDownload::completed);
        transport.start("model-test","test","/unused","/unused",10);
        QCOMPARE(done.count(),1);QVERIFY(!done.at(0).at(0).toString().isEmpty());
    }
    void storeCatalogPinsFocusedModelAndDependencies(){
        const auto assets=ModelManager::assets();
        int denoise=0,landmarker=0;
        for(const auto &a:assets){
            if(a.path=="ditto-original/denoise.onnx"){
                ++denoise;QVERIFY(a.appleOnly);QVERIFY(a.repo.isEmpty());
                QCOMPARE(a.sha256,QString("bb30d10ccc0ef8746c2623b558cebb8fa6e37dbc1ec0f7bdc04a7243fafa3164"));
            }
            if(a.path=="ditto-original/face_landmarker.task"){++landmarker;QVERIFY(a.appleOnly);}
            if(!a.resource.isEmpty()){
                QFile file(a.resource);QVERIFY(file.open(QIODevice::ReadOnly));const auto data=file.readAll();
                QCOMPARE(data.size(),a.bytes);
                QCOMPARE(QString::fromLatin1(QCryptographicHash::hash(data,QCryptographicHash::Sha256).toHex()),a.sha256);
            }
        }
        QCOMPARE(denoise,1);QCOMPARE(landmarker,1);
    }
    void appleOnlyNeverUsesMirror(){
        QStandardPaths::setTestModeEnabled(true);
        const QByteArray catalog=R"({"groups":[{"id":"isolated-test"}],"files":[{"group":"isolated-test","path":"isolated-channel-test.bin","file":"never-fetch","sha256":"test","bytes":10,"appleOnly":true}]})";
        ModelManager m(nullptr,catalog,QUrl("http://127.0.0.1:1/"));
        m.setEndpoint("https://modelscope.cn");QSignalSpy done(&m,&ModelManager::completed);
        m.requestDownload("isolated-test");m.startDownload();
        QTRY_COMPARE(done.count(),1);QVERIFY(!done.at(0).at(0).toBool());
        QVERIFY(m.error().contains("App Store"));QVERIFY(!m.error().contains("镜像"));
    }
};
QTEST_GUILESS_MAIN(StoreChannelTests)
#include "store_model_channel_tests.moc"
