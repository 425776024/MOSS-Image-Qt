#include "RuntimeManager.h"
#include <QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QRegularExpression>
#include <QStandardPaths>

namespace {
QByteArray read(const QString &path) {QFile file(path);return file.open(QIODevice::ReadOnly)?file.readAll():QByteArray();}
class Server:public QObject {
public:
    QTcpServer server;
    QMap<QString,QByteArray> files;
    QList<QPair<QString,qint64>> requests;
    bool corrupt=false;
    Server() {
        server.listen(QHostAddress::LocalHost);
        connect(&server,&QTcpServer::newConnection,this,[this]{
            while(server.hasPendingConnections()) {
                auto socket=server.nextPendingConnection();
                connect(socket,&QTcpSocket::readyRead,socket,[this,socket]{
                    auto request=socket->property("request").toByteArray()+socket->readAll();socket->setProperty("request",request);
                    if(!request.contains("\r\n\r\n")||socket->property("handled").toBool())return;
                    socket->setProperty("handled",true);
                    const auto name=QString::fromLatin1(request.split(' ').value(1).mid(1));auto bytes=files.value(name);
                    if(corrupt)bytes.fill('x');
                    const auto range=QRegularExpression("Range: bytes=([0-9]+)-([0-9]*)",QRegularExpression::CaseInsensitiveOption).match(QString::fromLatin1(request));
                    qint64 begin=range.hasMatch()?range.captured(1).toLongLong():0;
                    const qint64 end=range.hasMatch()&&!range.captured(2).isEmpty()?range.captured(2).toLongLong()+1:bytes.size();
                    requests.append({name,begin});
                    const QByteArray status=range.hasMatch()?QByteArrayLiteral("HTTP/1.1 206 Partial Content\r\nContent-Range: bytes ")+QByteArray::number(begin)+"-"+QByteArray::number(end-1)+"/"+QByteArray::number(bytes.size())+"\r\n":QByteArrayLiteral("HTTP/1.1 200 OK\r\n");
                    socket->write(status+"Content-Length: "+QByteArray::number(end-begin)+"\r\nConnection: close\r\n\r\n");
                    auto timer=new QTimer(socket);timer->setInterval(3);
                    connect(timer,&QTimer::timeout,socket,[socket,timer,bytes,begin,end]() mutable {
                        if(socket->state()!=QTcpSocket::ConnectedState){timer->stop();return;}
                        const auto count=std::min<qint64>(16384,end-begin);socket->write(bytes.mid(begin,count));begin+=count;
                        if(begin==end){timer->stop();socket->disconnectFromHost();}
                    });timer->start();
                });
                connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
            }
        });
    }
    QUrl base() const {return QUrl(QString("http://127.0.0.1:%1/").arg(server.serverPort()));}
    void add(const QString &directory) {for(const auto &name:QDir(directory).entryList({"*.zip"},QDir::Files))files[name]=read(directory+"/"+name);}
};
}
class RuntimeTests:public QObject {
    Q_OBJECT
    QTemporaryDir fixtures;
    QByteArray manifest(int version=1) {return read(fixtures.path()+QString("/v%1/cuda-runtime.json").arg(version));}
    bool extractionAvailable() {
#ifdef Q_OS_WIN
        return true;
#else
        return !QStandardPaths::findExecutable("pwsh").isEmpty();
#endif
    }
private slots:
    void initTestCase() {
        QProcess fixture;fixture.start(MOSS_TEST_PYTHON,{QString(MOSS_SOURCE_ROOT)+"/tests/runtime_fixture.py",fixtures.path()});
        QVERIFY(fixture.waitForFinished(30000));QVERIFY2(fixture.exitCode()==0,fixture.readAllStandardError().constData());
    }
    void downloadThenReuseAcrossAppUpdatesAndOnlyFetchChangedComponent() {
        if(!extractionAvailable())QSKIP("PowerShell is required to test the real Windows extraction script");
        QTemporaryDir cache;Server server;server.add(fixtures.path()+"/v1");server.add(fixtures.path()+"/v2");
        RuntimeManager first(nullptr,server.base());QSignalSpy done(&first,&RuntimeManager::completed);
        first.prepare(manifest(),cache.path());QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,30000);
        QVERIFY2(done[0][1].toString().isEmpty(),qPrintable(done[0][1].toString()));QVERIFY(!done[0][2].toBool());
        const auto original=done[0][0].toString();QVERIFY(QFileInfo::exists(original+"/cudnn64_9.dll"));
        QVERIFY(server.requests.size()>=2);server.requests.clear();
        RuntimeManager updatedApp(nullptr,server.base());QSignalSpy reused(&updatedApp,&RuntimeManager::completed);
        updatedApp.prepare(manifest(),cache.path());QTRY_COMPARE_WITH_TIMEOUT(reused.size(),1,30000);
        QCOMPARE(reused[0][0].toString(),original);QVERIFY(server.requests.isEmpty());
        RuntimeManager newEngine(nullptr,server.base());QSignalSpy changed(&newEngine,&RuntimeManager::completed);
        newEngine.prepare(manifest(2),cache.path());QTRY_COMPARE_WITH_TIMEOUT(changed.size(),1,30000);
        QVERIFY2(changed[0][1].toString().isEmpty(),qPrintable(changed[0][1].toString()));
        QVERIFY(changed[0][0].toString()!=original);
        QVERIFY(QFileInfo::exists(original+"/cudnn64_9.dll"));
        QVERIFY(!server.requests.isEmpty());for(const auto &request:server.requests)QVERIFY(request.first.startsWith("onnxruntime-cuda-"));
        QCOMPARE(read(original+"/cudnn64_9.dll"),read(changed[0][0].toString()+"/cudnn64_9.dll"));
    }
    void cancelAndResumeDownload() {
        if(!extractionAvailable())QSKIP("PowerShell is required");
        QTemporaryDir cache;Server server;server.add(fixtures.path()+"/v1");
        RuntimeManager first(nullptr,server.base());QSignalSpy cancelled(&first,&RuntimeManager::completed);
        connect(&first,&RuntimeManager::progress,&first,[&](double value){if(value>0.05)first.cancel();});
        first.prepare(manifest(),cache.path());QTRY_COMPARE_WITH_TIMEOUT(cancelled.size(),1,30000);
        QVERIFY(cancelled[0][2].toBool());QVERIFY(!first.busy());server.requests.clear();
        RuntimeManager second(nullptr,server.base());QSignalSpy done(&second,&RuntimeManager::completed);
        second.prepare(manifest(),cache.path());QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,30000);
        QVERIFY2(done[0][1].toString().isEmpty(),qPrintable(done[0][1].toString()));
        bool resumed=false;for(const auto &request:server.requests)resumed|=request.second>0;QVERIFY(resumed);
    }
    void corruptDownloadNeverBecomesUsableAndRetrySucceeds() {
        if(!extractionAvailable())QSKIP("PowerShell is required");
        QTemporaryDir cache;Server server;server.add(fixtures.path()+"/v1");server.corrupt=true;
        RuntimeManager first(nullptr,server.base());QSignalSpy bad(&first,&RuntimeManager::completed);
        first.prepare(manifest(),cache.path());QTRY_COMPARE_WITH_TIMEOUT(bad.size(),1,30000);
        QVERIFY(!bad[0][1].toString().isEmpty());QVERIFY(bad[0][0].toString().isEmpty());
        QVERIFY(QDir(cache.path()+"/sets").entryList(QDir::Dirs|QDir::NoDotAndDotDot).isEmpty());
        server.corrupt=false;RuntimeManager retry(nullptr,server.base());QSignalSpy done(&retry,&RuntimeManager::completed);
        retry.prepare(manifest(),cache.path());QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,30000);
        QVERIFY2(done[0][1].toString().isEmpty(),qPrintable(done[0][1].toString()));
    }
    void corruptCachedComponentRepairsOnlyThatComponent() {
        if(!extractionAvailable())QSKIP("PowerShell is required");
        QTemporaryDir cache;Server server;server.add(fixtures.path()+"/v1");
        RuntimeManager first(nullptr,server.base());QSignalSpy ready(&first,&RuntimeManager::completed);
        first.prepare(manifest(),cache.path());QTRY_COMPARE_WITH_TIMEOUT(ready.size(),1,30000);
        QVERIFY(ready[0][1].toString().isEmpty());const auto set=ready[0][0].toString();
        const auto original=read(set+"/cudnn64_9.dll");
        const auto components=QJsonDocument::fromJson(manifest()).object()["components"].toArray();
        QString id;for(const auto &c:components)if(c.toObject()["name"]=="cuda-libraries")id=c.toObject()["id"].toString();
        QFile damaged(cache.path()+"/components/"+id+"/cudnn64_9.dll");QVERIFY(damaged.open(QIODevice::WriteOnly));
        damaged.write(QByteArray(original.size(),'x'));damaged.close();server.requests.clear();
        RuntimeManager repair(nullptr,server.base());QSignalSpy done(&repair,&RuntimeManager::completed);
        repair.prepare(manifest(),cache.path());QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,30000);
        QVERIFY2(done[0][1].toString().isEmpty(),qPrintable(done[0][1].toString()));
        QCOMPARE(read(done[0][0].toString()+"/cudnn64_9.dll"),original);
        QVERIFY(!server.requests.isEmpty());for(const auto &request:server.requests)QVERIFY(request.first.startsWith("cuda-libraries-"));
    }
    void anotherWindowCannotMutateAnActiveCache() {
        QTemporaryDir cache;QLockFile lock(cache.path()+"/.prepare.lock");lock.setStaleLockTime(0);QVERIFY(lock.tryLock());
        Server server;RuntimeManager manager(nullptr,server.base());QSignalSpy done(&manager,&RuntimeManager::completed);
        manager.prepare(manifest(),cache.path());QCOMPARE(done.size(),1);
        QVERIFY(!done[0][1].toString().isEmpty());QVERIFY(server.requests.isEmpty());QVERIFY(lock.isLocked());
    }
    void invalidManifestDoesNotStartNetwork() {
        QTemporaryDir cache;Server server;
        auto object=QJsonDocument::fromJson(manifest()).object();auto components=object["components"].toArray();
        auto component=components[0].toObject();component["url"]="https://example.com/runtime.zip";components[0]=component;object["components"]=components;
        RuntimeManager manager(nullptr,server.base());QSignalSpy done(&manager,&RuntimeManager::completed);
        manager.prepare(QJsonDocument(object).toJson(),cache.path());QCOMPARE(done.size(),1);
        QVERIFY(!done[0][1].toString().isEmpty());QVERIFY(server.requests.isEmpty());
    }
};
QTEST_GUILESS_MAIN(RuntimeTests)
#include "runtime_manager_tests.moc"
