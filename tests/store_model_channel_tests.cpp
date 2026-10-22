#include <QtTest>
#include <QSettings>
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
        m.setEndpoint("https://hf-mirror.com");
        QCOMPARE(QSettings().value("storeModelEndpoint").toString(),QString("https://hf-mirror.com"));
        QCOMPARE(QSettings().value("modelEndpoint").toString(),QString("https://huggingface.co"));
        ModelManager restored(nullptr,"{\"groups\":[],\"files\":[]}");
        QCOMPARE(restored.endpoint(),QString("https://hf-mirror.com"));
    }
    void unconfiguredBundleFailsWithoutCallingApple(){
        AppleAssetDownload transport;QSignalSpy done(&transport,&AppleAssetDownload::completed);
        transport.start("model-test","test","/unused","/unused",10);
        QCOMPARE(done.count(),1);QVERIFY(!done.at(0).at(0).toString().isEmpty());
    }
};
QTEST_GUILESS_MAIN(StoreChannelTests)
#include "store_model_channel_tests.moc"
