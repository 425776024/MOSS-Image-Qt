#include "PurchaseManager.h"
#include <QtTest>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlPropertyMap>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>

// Test-only bridge. The shipping target links Apple's StoreKit implementation.
static int purchases=0,restores=0,trials=0;
extern "C" {
void *moss_store_create(void *,void (*)(void *,const char *)){return nullptr;}
void moss_store_destroy(void *){}
void moss_store_refresh(void *){}
void moss_store_purchase(void *){++purchases;}
void moss_store_restore(void *){++restores;}
void moss_store_trial(void *){++trials;}
}

class PurchaseManagerTests:public QObject {
    Q_OBJECT
    static void state(PurchaseManager &manager,bool ready,bool owned,bool available=false,QString message={}) {
        manager.receive(QJsonDocument(QJsonObject{{"ready",ready},{"unlocked",owned},{"available",available},
            {"busy",false},{"trialAvailable",available},{"price",available?"¥20.00":""},{"message",message}}).toJson());
    }
private slots:
    void entitlementLifecycle(){
        PurchaseManager manager;QSignalSpy paywall(&manager,&PurchaseManager::unlockRequested);
        QVERIFY(!manager.ready());QVERIFY(!manager.requireAccess());QCOMPARE(paywall.size(),1);
        // Incomplete responses never grant access, even if they claim ownership.
        state(manager,false,true);QVERIFY(!manager.lifetimeUnlocked());
        state(manager,true,false,true);QVERIFY(manager.canPurchase());
        const int before=purchases;manager.purchase();manager.purchase();
        QCOMPARE(purchases,before+1);QVERIFY(manager.busy());QVERIFY(!manager.canPurchase());
        state(manager,true,false,true,"pending");QVERIFY(!manager.lifetimeUnlocked());QVERIFY(!manager.message().isEmpty());
        state(manager,true,true,true);QVERIFY(manager.requireAccess());QVERIFY(!manager.canPurchase());
        // Losing the product's network price must not lock out a verified owner.
        state(manager,true,true,false,"unavailable");QVERIFY(manager.lifetimeUnlocked());
        // Refund/revocation replaces ownership instead of keeping a sticky flag.
        state(manager,true,false,true);QVERIFY(!manager.requireAccess());
        const int restoreBefore=restores;manager.restore();manager.restore();QCOMPARE(restores,restoreBefore+1);
        state(manager,true,true,true,"restored");QVERIFY(manager.lifetimeUnlocked());
    }
    void trialLifecycle(){
        PurchaseManager manager;QVERIFY(!manager.accessAllowed());
        auto now=QDateTime::currentMSecsSinceEpoch();
        auto deliver=[&](qint64 expires,bool started,bool owned=false){manager.receive(QJsonDocument(QJsonObject{
            {"ready",true},{"unlocked",owned},{"available",true},{"trialAvailable",true},{"trialStarted",started},
            {"trialExpires",double(expires)},{"now",double(now)},{"price","¥20.00"}}).toJson());};
        deliver(0,false);QVERIFY(manager.canStartTrial());
        int before=trials;manager.startTrial();manager.startTrial();QCOMPARE(trials,before+1);
        deliver(now+259200000,true);QVERIFY(manager.accessAllowed());QVERIFY(!manager.canStartTrial());QCOMPARE(manager.trialDaysRemaining(),3);
        deliver(now-1,true);QVERIFY(!manager.accessAllowed());QVERIFY(!manager.canStartTrial());QCOMPARE(manager.trialDaysRemaining(),0);
        deliver(now-1,true,true);QVERIFY(manager.accessAllowed());
        // A signed/server clock ahead of local time must not revive an expired trial.
        now+=259200001;deliver(now-1,true);QVERIFY(!manager.accessAllowed());
    }
    void dialog_data(){
        QTest::addColumn<QString>("language");QTest::addColumn<QString>("appearance");
        for(const auto &language:{"zh","en"})for(const auto &appearance:{"light","dark"})
            QTest::newRow(qPrintable(QString(language)+"-"+appearance))<<QString(language)<<QString(appearance);
    }
    void dialog(){
        QFETCH(QString,language);QFETCH(QString,appearance);
        PurchaseManager manager;state(manager,true,false,true);
        QTemporaryDir root;QDir qml(root.filePath("qml"));QVERIFY(QDir().mkpath(qml.path()));
        const auto source=QFINDTESTDATA("../qml");
        for(const auto &file:QDir(source).entryList({"*.qml","*.js"},QDir::Files))QVERIFY(QFile::copy(source+"/"+file,qml.filePath(file)));
        QFile types(qml.filePath("qmldir"));QVERIFY(types.open(QIODevice::WriteOnly));
        types.write("singleton HomeTheme 1.0 HomeTheme.qml\nsingleton VideoCutTheme 1.0 VideoCutTheme.qml\nsingleton StudioTheme 1.0 StudioTheme.qml\n");types.close();
        QQmlPropertyMap workspace,studio;workspace.insert("language",language);workspace.insert("appearanceMode",appearance);workspace.insert("themePreset","blue");
        studio.insert("purchases",QVariant::fromValue(static_cast<QObject *>(&manager)));
        QQmlApplicationEngine engine;engine.rootContext()->setContextProperty("workspace",&workspace);engine.rootContext()->setContextProperty("studio",&studio);
        engine.loadData("import QtQuick\nimport QtQuick.Controls.Basic\nimport \".\"\nApplicationWindow { visible:true; width:1080; height:600; color:HomeTheme.surface; PurchaseDialog {} }",QUrl::fromLocalFile(qml.filePath("Harness.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());auto window=qobject_cast<QQuickWindow *>(engine.rootObjects().first());QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        window->requestActivate();
        if(QGuiApplication::platformName()=="cocoa")QVERIFY(QTest::qWaitForWindowActive(window));
        auto dialog=window->findChild<QObject *>("purchaseDialog");QVERIFY(dialog);
        manager.showUnlock();QTRY_VERIFY(dialog->property("opened").toBool());
        auto buy=window->findChild<QQuickItem *>("buyLifetime");auto restore=window->findChild<QQuickItem *>("restorePurchases");
        QVERIFY(buy);QVERIFY(restore);QVERIFY(buy->isEnabled());QVERIFY(buy->property("text").toString().contains("¥20.00"));
        const QRectF bounds(QPointF(),window->size());
        for(auto item:{buy,restore})QVERIFY(bounds.contains(QRectF(item->mapToScene(QPointF()),item->size())));
        const auto evidence=qEnvironmentVariable("MOSS_PURCHASE_EVIDENCE");
        if(!evidence.isEmpty()){
            QDir().mkpath(evidence);
            if(qEnvironmentVariableIsSet("MOSS_PURCHASE_REVIEW_CAPTURE"))window->resize(1280,800);
            QTest::qWait(100);
            QVERIFY(window->grabWindow().save(evidence+"/purchase-"+language+"-"+appearance+".png"));
        }
        const int before=purchases;buy->forceActiveFocus(Qt::TabFocusReason);QTest::keyClick(window,Qt::Key_Space);
        QTRY_COMPARE(purchases,before+1);QVERIFY(!buy->isEnabled());
        state(manager,true,false,true);restore->forceActiveFocus(Qt::TabFocusReason);
        const int restoreBefore=restores;QTest::keyClick(window,Qt::Key_Space);QTRY_COMPARE(restores,restoreBefore+1);
        state(manager,true,true,true,"restored");QTRY_VERIFY(!buy->isVisible());
        QTest::keyClick(window,Qt::Key_Escape);QTRY_VERIFY(!dialog->property("visible").toBool());
        state(manager,true,true,false,"unavailable");manager.showUnlock();QTRY_VERIFY(dialog->property("opened").toBool());
        QVERIFY(!buy->isVisible());QVERIFY(restore->isEnabled());
        QTest::keyClick(window,Qt::Key_Period,Qt::ControlModifier);QTRY_VERIFY(!dialog->property("visible").toBool());
    }
};
QTEST_MAIN(PurchaseManagerTests)
#include "purchase_manager_tests.moc"
