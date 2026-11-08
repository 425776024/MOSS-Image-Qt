#include "ModelManager.h"
#include <QtTest>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlPropertyMap>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QSettings>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonArray>
#include <QRegularExpression>
#include <functional>

class DownloadDialogTests:public QObject {
    Q_OBJECT
private slots:
    void incompleteResourcesShowFullSizeAndFailureDetails_data() {
        slowDownloadSpeedAndResume_data();
    }
    void incompleteResourcesShowFullSizeAndFailureDetails() {
        QFETCH(QString,language);QFETCH(QString,appearance);
        QTemporaryDir root;QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root.path());QSettings().clear();
        QTcpServer server;QVERIFY(server.listen(QHostAddress::LocalHost));bool retry=false;
        const QByteArray body(465746,'s');
        connect(&server,&QTcpServer::newConnection,this,[&]{
            auto socket=server.nextPendingConnection();
            connect(socket,&QTcpSocket::readyRead,socket,[&,socket]{
                const auto request=socket->property("request").toByteArray()+socket->readAll();socket->setProperty("request",request);
                if(!request.contains("\r\n\r\n")||socket->property("handled").toBool())return;
                socket->setProperty("handled",true);
                if(!retry)socket->write("HTTP/1.1 503 Unavailable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                else socket->write("HTTP/1.1 206 Partial Content\r\nContent-Range: bytes 0-465745/465746\r\nContent-Length: 465746\r\nConnection: close\r\n\r\n"+body);
                socket->disconnectFromHost();
            });connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
        });
        QJsonArray files,groups;
        const auto hash=[](const QByteArray &data){return QString::fromLatin1(QCryptographicHash::hash(data,QCryptographicHash::Sha256).toHex());};
        for(const auto &id:{"image-q4","image-shared","ditto","voice","sfx"}) {
            groups.append(QJsonObject{{"id",id},{"title",id}});
            files.append(QJsonObject{{"group",id},{"path",QString(id)+"/support.bin"},{"file","support"},
                {"bytes",body.size()},{"sha256",hash(body)}});
        }
        files.append(QJsonObject{{"group","ditto"},{"path","ditto/existing.bin"},{"bytes",2000000},
            {"sha256",hash(QByteArray(2000000,'e'))}});
        QVERIFY(QDir().mkpath(root.filePath("models/ditto")));
        QFile installed(root.filePath("models/ditto/existing.bin"));QVERIFY(installed.open(QIODevice::WriteOnly));
        QCOMPARE(installed.write(QByteArray(2000000,'e')),qint64(2000000));installed.close();
        ModelManager manager(nullptr,QJsonDocument(QJsonObject{{"groups",groups},{"files",files}}).toJson(),QUrl(QString("http://127.0.0.1:%1/").arg(server.serverPort())));
        manager.setRoot(root.filePath("models"));
        const auto source=QFINDTESTDATA("../qml");QDir qml(root.filePath("qml"));QVERIFY(QDir().mkpath(qml.path()));
        for(const auto &file:QDir(source).entryList({"*.qml","*.js"},QDir::Files))QVERIFY(QFile::copy(source+"/"+file,qml.filePath(file)));
        QFile types(qml.filePath("qmldir"));QVERIFY(types.open(QIODevice::WriteOnly));
        types.write("singleton HomeTheme 1.0 HomeTheme.qml\nsingleton VideoCutTheme 1.0 VideoCutTheme.qml\nsingleton StudioTheme 1.0 StudioTheme.qml\n");types.close();
        QQmlPropertyMap workspace,studio;workspace.insert("language",language);workspace.insert("appearanceMode",appearance);workspace.insert("themePreset","blue");
        studio.insert("models",QVariant::fromValue(static_cast<QObject *>(&manager)));
        QQmlApplicationEngine engine;engine.rootContext()->setContextProperty("workspace",&workspace);engine.rootContext()->setContextProperty("studio",&studio);
        engine.loadData("import QtQuick\nimport QtQuick.Controls.Basic\nimport \".\"\nApplicationWindow { visible:true; width:1080; height:600; ModelDownloadDialog {} }",QUrl::fromLocalFile(qml.filePath("Harness.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());auto window=qobject_cast<QQuickWindow *>(engine.rootObjects().first());QVERIFY(window);
        window->raise();window->requestActivate();QVERIFY(QTest::qWaitForWindowExposed(window));
        auto dialog=window->findChild<QObject *>("modelDownloadDialog");QVERIFY(dialog);
        manager.requestDownload("ditto");QTRY_VERIFY(dialog->property("opened").toBool());
        std::function<QQuickItem *(QQuickItem *,const QString &)> findItem=[&](QQuickItem *parent,const QString &name)->QQuickItem * {
            if(parent->objectName()==name)return parent;
            for(auto child:parent->childItems())if(auto found=findItem(child,name))return found;
            return nullptr;
        };
        auto size=findItem(window->contentItem(),"setupResourceSize-ditto");QVERIFY(size);
        const auto fullSize=manager.resources()[1].toMap()["totalSize"].toString();
        QVERIFY(size->property("text").toString().contains(fullSize));
        auto remainder=window->findChild<QObject *>("setupDownloadSize");QVERIFY(remainder);
        QVERIFY(remainder->property("text").toString().contains(manager.requestInfo()["size"].toString()));
        manager.startDownload();QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(),4000);QVERIFY(manager.error().contains("HTTP 503"));
        auto toggle=window->findChild<QQuickItem *>("toggleDownloadErrorDetails");QVERIFY(toggle);QVERIFY(toggle->isVisible());
        const auto activate=[&](QQuickItem *item) {
            item->forceActiveFocus(Qt::TabFocusReason);
            if(QGuiApplication::platformName()=="offscreen")QTest::keyClick(window,Qt::Key_Space);
            else QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,item->mapToScene(item->boundingRect().center()).toPoint());
        };
        activate(toggle);QTRY_VERIFY(dialog->property("detailsExpanded").toBool());
        auto detail=window->findChild<QQuickItem *>("downloadErrorDetail");QVERIFY(detail);QVERIFY(detail->isVisible());
        QCOMPARE(detail->property("text").toString(),manager.error());QVERIFY(detail->property("readOnly").toBool());
        auto change=window->findChild<QQuickItem *>("changeDownloadSource");QVERIFY(change);activate(change);
        QTRY_VERIFY(dialog->property("sourceExpanded").toBool());
        auto confirm=window->findChild<QQuickItem *>("confirmModelDownload");QVERIFY(confirm);
        const QRectF bounds(QPointF(),window->size());
        for(auto item:{toggle,detail,change,confirm})QVERIFY(bounds.contains(QRectF(item->mapToScene(QPointF()),item->size())));
        const auto evidence=qEnvironmentVariable("MOSS_DOWNLOAD_DIALOG_EVIDENCE");
        if(!evidence.isEmpty()){QVERIFY(QDir().mkpath(evidence));QTest::qWait(100);QVERIFY(window->grabWindow().save(evidence+"/failure-"+language+"-"+appearance+".png"));}
        retry=true;activate(confirm);QTRY_VERIFY_WITH_TIMEOUT(!manager.busy()&&manager.ready("ditto"),4000);
        QVERIFY(manager.error().isEmpty());QTRY_VERIFY(dialog->property("ready").toBool());
        auto close=window->findChild<QQuickItem *>("closeModelDownload");QVERIFY(close);activate(close);
        QTRY_VERIFY(!dialog->property("visible").toBool());
    }
    void slowDownloadSpeedAndResume_data() {
        QTest::addColumn<QString>("language");QTest::addColumn<QString>("appearance");
        QTest::newRow("zh-light")<<QString("zh")<<QString("light");
        QTest::newRow("zh-dark")<<QString("zh")<<QString("dark");
        QTest::newRow("en-light")<<QString("en")<<QString("light");
        QTest::newRow("en-dark")<<QString("en")<<QString("dark");
    }
    void slowDownloadSpeedAndResume() {
        QFETCH(QString,language);QFETCH(QString,appearance);
        QTemporaryDir root;QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root.path());QSettings().clear();
        QTcpServer server;QVERIFY(server.listen(QHostAddress::LocalHost));
        const QByteArray body(4*1024*1024,'d');bool resume=false;bool resumed=false;
        connect(&server,&QTcpServer::newConnection,this,[&]{
            auto socket=server.nextPendingConnection();
            connect(socket,&QTcpSocket::readyRead,socket,[&,socket]{
                auto request=socket->property("request").toByteArray()+socket->readAll();socket->setProperty("request",request);
                if(!request.contains("\r\n\r\n")||socket->property("handled").toBool())return;
                socket->setProperty("handled",true);
                auto match=QRegularExpression("Range: bytes=([0-9]+)-([0-9]+)",QRegularExpression::CaseInsensitiveOption).match(QString::fromLatin1(request));
                const auto offset=match.captured(1).toLongLong(),end=match.captured(2).toLongLong()+1;
                socket->write("HTTP/1.1 206 Partial Content\r\nContent-Range: bytes "+QByteArray::number(offset)+"-"+QByteArray::number(end-1)+"/"+QByteArray::number(body.size())+"\r\nContent-Length: "+QByteArray::number(end-offset)+"\r\n\r\n");
                if(resume){resumed=offset>0;socket->write(body.mid(offset,end-offset));socket->disconnectFromHost();}
                else socket->write(body.left(160*1024));
            });
            connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
        });
        QJsonObject asset{{"group","ditto"},{"path","ditto/model.bin"},{"file","file"},{"bytes",body.size()},
            {"sha256",QString::fromLatin1(QCryptographicHash::hash(body,QCryptographicHash::Sha256).toHex())}};
        const auto catalog=QJsonDocument(QJsonObject{{"groups",QJsonArray{QJsonObject{{"id","ditto"},{"title","数字人"}}}},{"files",QJsonArray{asset}}}).toJson();
        ModelManager manager(nullptr,catalog,QUrl(QString("http://127.0.0.1:%1/").arg(server.serverPort())));manager.setRoot(root.filePath("models"));
        // Load exact production QML locally, declaring its existing singletons.
        const auto source=QFINDTESTDATA("../qml");QDir qml(root.filePath("qml"));QVERIFY(QDir().mkpath(qml.path()));
        for(const auto &file:QDir(source).entryList({"*.qml","*.js"},QDir::Files))QVERIFY(QFile::copy(source+"/"+file,qml.filePath(file)));
        QFile types(qml.filePath("qmldir"));QVERIFY(types.open(QIODevice::WriteOnly));
        types.write("singleton HomeTheme 1.0 HomeTheme.qml\nsingleton VideoCutTheme 1.0 VideoCutTheme.qml\nsingleton StudioTheme 1.0 StudioTheme.qml\n");types.close();
        QQmlPropertyMap workspace,studio;workspace.insert("language",language);workspace.insert("appearanceMode",appearance);workspace.insert("themePreset","blue");
        studio.insert("models",QVariant::fromValue(static_cast<QObject *>(&manager)));
        QQmlApplicationEngine engine;engine.rootContext()->setContextProperty("workspace",&workspace);engine.rootContext()->setContextProperty("studio",&studio);
        engine.loadData("import QtQuick\nimport QtQuick.Controls.Basic\nimport \".\"\nApplicationWindow { visible:true; width:960; height:640; ModelDownloadDialog {} }",QUrl::fromLocalFile(qml.filePath("Harness.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());auto window=qobject_cast<QQuickWindow *>(engine.rootObjects().first());QVERIFY(window);
        if(QGuiApplication::platformName()!="offscreen")window->resize(1080,600);
        window->raise();window->requestActivate();
        if(QGuiApplication::platformName()=="offscreen")QVERIFY(QTest::qWaitForWindowActive(window,3000));
        auto dialog=window->findChild<QObject *>("modelDownloadDialog");QVERIFY(dialog);
        manager.requestDownload("ditto");QTRY_VERIFY(dialog->property("opened").toBool());manager.startDownload();
        QTRY_VERIFY(manager.progress()>0);QVERIFY(!dialog->property("sourceExpanded").toBool());
        auto speed=window->findChild<QQuickItem *>("modelDownloadSpeed");QVERIFY(speed);QVERIFY(speed->isVisible());
        QTRY_VERIFY(manager.downloadBytesPerSecond()>0);
        QVERIFY(speed->property("text").toString().contains("/s"));
        const auto capture=qEnvironmentVariable("MOSS_DOWNLOAD_DIALOG_EVIDENCE");
        if(!capture.isEmpty()){QVERIFY(QDir().mkpath(capture));QVERIFY(window->grabWindow().save(capture+"/speed-"+language+"-"+appearance+".png"));}
        QTest::qWait(5500);QCOMPARE(manager.downloadBytesPerSecond(),0.0);
        QVERIFY(manager.busy());QVERIFY(!manager.paused());
        manager.cancel();QTRY_VERIFY(!manager.busy());
        auto change=window->findChild<QQuickItem *>("changeDownloadSource");QVERIFY(change);
        change->forceActiveFocus(Qt::TabFocusReason);QTest::keyClick(window,Qt::Key_Space);
        QTRY_VERIFY(dialog->property("sourceExpanded").toBool());
        QTRY_VERIFY(dialog->property("opened").toBool());
        auto choices=window->findChild<QQuickItem *>("downloadSourceChoices");QVERIFY(choices);QTRY_VERIFY(choices->isVisible());
        auto confirm=window->findChild<QQuickItem *>("confirmModelDownload");QVERIFY(confirm);QVERIFY(confirm->isVisible());
        QVERIFY(confirm->isEnabled());
        const QRectF bounds(QPointF(0,0),window->size());
        std::function<QQuickItem *(QQuickItem *,const QString &)> findItem=[&](QQuickItem *parent,const QString &name)->QQuickItem * {
            if(parent->objectName()==name)return parent;
            for(auto child:parent->childItems())if(auto found=findItem(child,name))return found;
            return nullptr;
        };
        for(int i=0;i<manager.sourceValues().size();++i) {
            auto radio=findItem(window->contentItem(),"downloadSource-"+QString::number(i));QVERIFY(radio);QVERIFY(radio->isVisible());
            QVERIFY(bounds.contains(QRectF(radio->mapToScene(QPointF()),radio->size())));
            radio->forceActiveFocus(Qt::TabFocusReason);QTest::keyClick(window,Qt::Key_Space);
            QCOMPARE(manager.endpoint(),manager.sourceValues()[i]);
            for(int j=0;j<manager.sourceValues().size();++j)QCOMPARE(findItem(window->contentItem(),"downloadSource-"+QString::number(j))->property("checked").toBool(),i==j);
        }
        QVERIFY(bounds.contains(QRectF(confirm->mapToScene(QPointF()),confirm->size())));
        if(!capture.isEmpty()){QVERIFY(QDir().mkpath(capture));QVERIFY(window->grabWindow().save(capture+"/"+language+"-"+appearance+".png"));}
        resume=true;confirm->forceActiveFocus(Qt::TabFocusReason);
        if(QGuiApplication::platformName()=="offscreen")QTest::keyClick(window,Qt::Key_Return);
        else QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,confirm->mapToScene(confirm->boundingRect().center()).toPoint());
        QTRY_VERIFY_WITH_TIMEOUT(!manager.busy()&&!manager.paused(),5000);QVERIFY(manager.error().isEmpty());QVERIFY(resumed);
        QTRY_VERIFY(dialog->property("ready").toBool());QVERIFY(manager.ready("ditto"));
        if(QGuiApplication::platformName()=="offscreen")QTest::keyClick(window,Qt::Key_Escape);
        else {
            auto close=window->findChild<QQuickItem *>("closeModelDownload");QVERIFY(close);
            QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,close->mapToScene(close->boundingRect().center()).toPoint());
        }
        QTRY_VERIFY(!dialog->property("visible").toBool());
    }
};
QTEST_MAIN(DownloadDialogTests)
#include "model_download_dialog_tests.moc"
