#include "ModelHttpDownload.h"
#ifdef Q_OS_UNIX
#include "ResumableAssetCopy.h"
#endif
#include <QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QCryptographicHash>

class HttpFixture:public QObject {
public:
    struct Request { QString path; qint64 begin=0,end=0; bool range=false; };
    QTcpServer server;
    QByteArray payload=QByteArray(256*1024,'m');
    QList<Request> requests;
    int active=0,peak=0;
    std::function<void(QTcpSocket *,Request)> handler;
    HttpFixture() {
        for(int i=0;i<payload.size();++i)payload[i]=char(i%251);
        server.listen(QHostAddress::LocalHost);
        connect(&server,&QTcpServer::newConnection,this,[this]{
            while(server.hasPendingConnections()) {
                auto socket=server.nextPendingConnection();socket->setParent(this);
                connect(socket,&QTcpSocket::readyRead,socket,[this,socket]{
                    auto data=socket->property("request").toByteArray()+socket->readAll();socket->setProperty("request",data);
                    if(!data.contains("\r\n\r\n")||socket->property("handled").toBool())return;
                    socket->setProperty("handled",true);++active;peak=std::max(peak,active);
                    Request r;r.path=QString::fromLatin1(data.split(' ').value(1));r.end=payload.size();
                    auto match=QRegularExpression("Range: bytes=([0-9]+)-([0-9]*)",QRegularExpression::CaseInsensitiveOption).match(QString::fromLatin1(data));
                    if(match.hasMatch()){r.range=true;r.begin=match.captured(1).toLongLong();if(!match.captured(2).isEmpty())r.end=match.captured(2).toLongLong()+1;}
                    requests<<r;if(handler)handler(socket,r);else respond(socket,r);
                });
                connect(socket,&QTcpSocket::disconnected,socket,[this,socket]{if(socket->property("handled").toBool())--active;socket->deleteLater();});
            }
        });
    }
    QUrl url(QString name="file")const {return QUrl(QString("http://127.0.0.1:%1/%2").arg(server.serverPort()).arg(name));}
    void respond(QTcpSocket *s,Request r,int delay=1,int chunk=4096,bool ignore=false,bool badRange=false,int dropAfter=-1) {
        if(ignore){r.begin=0;r.end=payload.size();}
        QByteArray header=ignore||!r.range?QByteArrayLiteral("HTTP/1.1 200 OK\r\n"):"HTTP/1.1 206 Partial Content\r\nContent-Range: bytes "+QByteArray::number(r.begin)+"-"+QByteArray::number(r.end-1)+"/"+QByteArray::number(payload.size()+(badRange?1:0))+"\r\n";
        s->write(header+"Content-Length: "+QByteArray::number(r.end-r.begin)+"\r\nConnection: close\r\n\r\n");
        auto timer=new QTimer(s);timer->setInterval(delay);
        connect(timer,&QTimer::timeout,s,[this,s,r,chunk,dropAfter,timer,offset=r.begin]()mutable{
            if(s->state()!=QAbstractSocket::ConnectedState){timer->stop();return;}
            if(dropAfter>=0&&offset-r.begin>=dropAfter){s->abort();timer->stop();return;}
            const auto size=std::min<qint64>(chunk,r.end-offset);s->write(payload.mid(offset,size));offset+=size;
            if(offset>=r.end){timer->stop();s->disconnectFromHost();}
        });timer->start();
    }
};
class HttpDownloadTests:public QObject {
    Q_OBJECT
    ModelHttpDownload::Policy policy() {ModelHttpDownload::Policy p;p.probeBytes=4096;p.probeMs=300;p.idleMs=1500;p.firstByteMs=1500;p.parallelThreshold=64*1024;return p;}
    QByteArray read(const QString &path) {QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};return f.readAll();}
    void start(ModelHttpDownload &d,QTemporaryDir &root,HttpFixture &s,bool automatic=false) {
        d.setPolicy(policy());d.start(root.filePath("model.part"),s.payload.size(),"pinned-hash",automatic?QList<QUrl>{s.url("a"),s.url("b")}:QList<QUrl>{s.url()},"test",automatic);
    }
private slots:
    void init(){ModelHttpDownload::clearSourceCache();}
    void parallelRangesResumeAfterCancellation() {
        QTemporaryDir root;HttpFixture s;s.handler=[&](auto socket,auto r){s.respond(socket,r,10);};
        ModelHttpDownload first;QSignalSpy done(&first,&ModelHttpDownload::completed);
        connect(&first,&ModelHttpDownload::progress,&first,[&](qint64 n){if(n>=48*1024)first.cancel();});
        start(first,root,s);QTRY_COMPARE(done.size(),1);QVERIFY(done[0][1].toBool());QVERIFY(s.peak>=4);
        auto saved=ModelHttpDownload::savedBytes(root.filePath("model.part"),s.payload.size(),"pinned-hash");QVERIFY(saved>=48*1024);QVERIFY(saved<s.payload.size());
        int previous=s.requests.size();ModelHttpDownload second;QSignalSpy resumed(&second,&ModelHttpDownload::completed);start(second,root,s);
        QTRY_COMPARE(resumed.size(),1);QVERIFY2(resumed[0][0].toString().isEmpty(),qPrintable(resumed[0][0].toString()));QCOMPARE(read(root.filePath("model.part")),s.payload);
        bool offset=false;for(int i=previous;i<s.requests.size();++i)offset|=s.requests[i].begin%(s.payload.size()/4)>0;QVERIFY(offset);
        QVERIFY(!QFileInfo::exists(ModelHttpDownload::journalPath(root.filePath("model.part"))));
    }
    void fastestSourceAndCache() {
        QTemporaryDir root;HttpFixture s;s.handler=[&](auto socket,auto r){
            // Only B answers the probe; timer coalescing must not decide the fixture's winner.
            if(r.path=="/a"&&r.end-r.begin<=4096)return;
            s.respond(socket,r,2);
        };
        ModelHttpDownload d;QSignalSpy selected(&d,&ModelHttpDownload::sourceSelected),done(&d,&ModelHttpDownload::completed);
        start(d,root,s,true);QTRY_COMPARE(done.size(),1);QCOMPARE(selected[0][0].toUrl(),s.url("b"));QCOMPARE(read(root.filePath("model.part")),s.payload);
        QTemporaryDir secondRoot;int previous=s.requests.size();ModelHttpDownload second;QSignalSpy secondDone(&second,&ModelHttpDownload::completed);start(second,secondRoot,s,true);
        QTRY_COMPARE(secondDone.size(),1);
        for(int i=previous;i<s.requests.size();++i){QCOMPARE(s.requests[i].path,QString("/b"));QVERIFY(s.requests[i].end-s.requests[i].begin>4096);}
    }
    void failureSwitchesSourceAndResumes() {
        QTemporaryDir root;HttpFixture s;s.handler=[&](auto socket,auto r){
            if(r.path=="/b"&&r.end-r.begin<=4096)return;
            s.respond(socket,r,r.path=="/a"?1:15,4096,false,false,r.path=="/a"&&r.end-r.begin>4096?8192:-1);
        };
        ModelHttpDownload d;QSignalSpy selected(&d,&ModelHttpDownload::sourceSelected),done(&d,&ModelHttpDownload::completed);start(d,root,s,true);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,6000);QVERIFY2(done[0][0].toString().isEmpty(),qPrintable(done[0][0].toString()));QVERIFY(selected.size()>=2);QCOMPARE(selected.first()[0].toUrl(),s.url("a"));QCOMPARE(selected.last()[0].toUrl(),s.url("b"));
        bool offset=false;for(auto r:s.requests)if(r.path=="/b"&&r.end-r.begin>4096)offset|=r.begin%65536>0;QVERIFY(offset);QCOMPARE(read(root.filePath("model.part")),s.payload);
    }
    void ignoredRangeFallsBackWithoutCorruption() {
        QTemporaryDir root;HttpFixture s;s.handler=[&](auto socket,auto r){s.respond(socket,r,1,4096,true);};
        ModelHttpDownload d;QSignalSpy done(&d,&ModelHttpDownload::completed);start(d,root,s);QTRY_COMPARE(done.size(),1);
        QVERIFY(done[0][0].toString().isEmpty());QCOMPARE(read(root.filePath("model.part")),s.payload);QVERIFY(s.requests.size()<=3);
    }
    void malformedRangeFailsBoundedly() {
        QTemporaryDir root;HttpFixture s;s.handler=[&](auto socket,auto r){s.respond(socket,r,1,4096,false,true);};
        ModelHttpDownload d;QSignalSpy done(&d,&ModelHttpDownload::completed);start(d,root,s);QTRY_COMPARE(done.size(),1);
        QVERIFY(!done[0][0].toString().isEmpty());QCOMPARE(s.requests.size(),2);QCOMPARE(ModelHttpDownload::savedBytes(root.filePath("model.part"),s.payload.size(),"pinned-hash"),0);
    }
    void cancelWhileProbingStopsAllRequests() {
        QTemporaryDir root;HttpFixture s;s.handler=[](auto,auto){};
        ModelHttpDownload d;QSignalSpy done(&d,&ModelHttpDownload::completed);start(d,root,s,true);QTRY_COMPARE(s.requests.size(),2);d.cancel();QCOMPARE(done.size(),1);QVERIFY(done[0][1].toBool());QTRY_COMPARE(s.active,0);
    }
    void delayedFirstByteSurvivesIdleBudget() {
        QTemporaryDir root;HttpFixture s;
        s.handler=[&](auto socket,auto r){QTimer::singleShot(1800,socket,[&,socket,r]{s.respond(socket,r);});};
        ModelHttpDownload d;auto p=policy();p.idleMs=500;p.firstByteMs=4000;d.setPolicy(p);
        QSignalSpy done(&d,&ModelHttpDownload::completed);
        d.start(root.filePath("model.part"),s.payload.size(),"hash",{s.url()},"delayed",false);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,10000);
        QVERIFY2(done[0][0].toString().isEmpty(),qPrintable(done[0][0].toString()));
        QCOMPARE(s.requests.size(),4);QCOMPARE(read(root.filePath("model.part")),s.payload);
    }
    void noResponseTimesOutAndSwitchesSource() {
        QTemporaryDir root;HttpFixture s;
        s.handler=[&](auto socket,auto r){if(r.path=="/b")s.respond(socket,r);};
        ModelHttpDownload d;auto p=policy();p.firstByteMs=700;d.setPolicy(p);
        QSignalSpy done(&d,&ModelHttpDownload::completed),selected(&d,&ModelHttpDownload::sourceSelected);
        // Skip probes to exercise timeout/failover directly.
        d.start(root.filePath("model.part"),s.payload.size(),"hash",{s.url("a"),s.url("b")},"timeout",false);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,6000);
        QVERIFY(done[0][0].toString().isEmpty());QCOMPARE(selected.size(),2);
        QCOMPARE(selected.last()[0].toUrl(),s.url("b"));QCOMPARE(read(root.filePath("model.part")),s.payload);
    }
    void stalledRangeIsDetectedWhileOtherRangesReceive() {
        QTemporaryDir root;HttpFixture s;
        s.handler=[&](auto socket,auto r){
            // One range stalls after a prefix while other ranges keep arriving.
            if(r.path=="/a"&&r.begin==0) {
                socket->write("HTTP/1.1 206 Partial Content\r\nContent-Range: bytes 0-"+QByteArray::number(r.end-1)+"/"+QByteArray::number(s.payload.size())+"\r\nContent-Length: "+QByteArray::number(r.end)+"\r\n\r\n"+s.payload.left(1024));
            } else s.respond(socket,r,r.path=="/a"?100:1,1024);
        };
        ModelHttpDownload d;auto p=policy();p.idleMs=600;p.firstByteMs=4000;d.setPolicy(p);
        QSignalSpy done(&d,&ModelHttpDownload::completed),selected(&d,&ModelHttpDownload::sourceSelected);
        d.start(root.filePath("model.part"),s.payload.size(),"hash",{s.url("a"),s.url("b")},"stalled",false);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,5000);
        QVERIFY(done[0][0].toString().isEmpty());QCOMPARE(selected.size(),2);
        bool resumed=false;for(auto r:s.requests)if(r.path=="/b"&&r.begin==1024)resumed=true;
        QVERIFY(resumed);QCOMPARE(read(root.filePath("model.part")),s.payload);
    }
    void noResponseFailureIncludesDiagnostics() {
        QTemporaryDir root;HttpFixture s;s.handler=[](auto,auto){};
        ModelHttpDownload d;auto p=policy();p.firstByteMs=500;d.setPolicy(p);
        QSignalSpy done(&d,&ModelHttpDownload::completed);
        d.start(root.filePath("model.part"),s.payload.size(),"hash",{s.url()},"offline",false);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,4000);
        const auto error=done[0][0].toString();QVERIFY(error.contains("HTTP 0"));
        QVERIFY(error.contains("127.0.0.1"));QVERIFY(error.contains("0 / 262144 bytes"));
        QCOMPARE(s.requests.size(),2);
    }
    void allSourcesUnavailableStop() {
        QTemporaryDir root;HttpFixture s;s.handler=[](auto socket,auto){socket->write("HTTP/1.1 503 Unavailable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");socket->disconnectFromHost();};
        ModelHttpDownload d;QSignalSpy done(&d,&ModelHttpDownload::completed);start(d,root,s,true);QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,6000);QVERIFY(!done[0][0].toString().isEmpty());QCOMPARE(s.requests.size(),6);
    }
    void staleJournalDoesNotReuseWrongVersion() {
        QTemporaryDir root;HttpFixture s;ModelHttpDownload first;QSignalSpy done(&first,&ModelHttpDownload::completed);start(first,root,s);first.cancel();
        ModelHttpDownload second;QSignalSpy failed(&second,&ModelHttpDownload::completed);second.start(root.filePath("model.part"),s.payload.size(),"different-version",{s.url()},"test",false);
        QCOMPARE(failed.size(),1);QVERIFY(!failed[0][0].toString().isEmpty());
    }
    void redirectsKeepRangeAndResumeHeaders() {
        QTemporaryDir root;HttpFixture s;
        s.handler=[&](auto socket,auto r){
            if(r.path=="/file"){socket->write("HTTP/1.1 302 Found\r\nLocation: /storage\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");socket->disconnectFromHost();}
            else s.respond(socket,r);
        };
        ModelHttpDownload d;QSignalSpy done(&d,&ModelHttpDownload::completed);start(d,root,s);QTRY_COMPARE(done.size(),1);
        QVERIFY(done[0][0].toString().isEmpty());QCOMPARE(read(root.filePath("model.part")),s.payload);
        for(auto r:s.requests)if(r.path=="/storage")QVERIFY(r.range);
    }
    void slowTransferReassesses() {
        QTemporaryDir root;HttpFixture s;
        s.handler=[&](auto socket,auto r){const bool probe=r.end-r.begin<=4096;
            if(r.path=="/b"&&probe)return; // Start on A without racing short probe timers.
            s.respond(socket,r,r.path=="/a"?(probe?1:200):1,probe?4096:(r.path=="/a"?512:4096));
        };
        // Keep the threshold between fixture rates, including shared-runner timer coalescing.
        ModelHttpDownload d;auto p=policy();p.slowMs=500;p.cooldownMs=500;p.slowBytesPerSecond=64*1024;d.setPolicy(p);
        QSignalSpy selected(&d,&ModelHttpDownload::sourceSelected),done(&d,&ModelHttpDownload::completed);
        d.start(root.filePath("model.part"),s.payload.size(),"hash",{s.url("a"),s.url("b")},"slow",true);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,8000);QVERIFY(done[0][0].toString().isEmpty());QVERIFY(selected.size()>=2);QCOMPARE(selected.first()[0].toUrl(),s.url("a"));QCOMPARE(selected.last()[0].toUrl(),s.url("b"));QCOMPARE(read(root.filePath("model.part")),s.payload);
    }
    void allSlowSourcesChooseFastestWithoutPingPong() {
        QTemporaryDir root;HttpFixture s;
        s.handler=[&](auto socket,auto r){
            const bool probe=r.end-r.begin<=4096;
            if(r.path=="/b"&&probe)return;
            s.respond(socket,r,probe?1:(r.path=="/a"?40:200),probe||r.path=="/a"?4096:1024);
        };
        ModelHttpDownload d;auto p=policy();p.connections=1;p.slowMs=500;p.cooldownMs=5000;
        p.slowBytesPerSecond=1000000;d.setPolicy(p);
        QSignalSpy selected(&d,&ModelHttpDownload::sourceSelected),done(&d,&ModelHttpDownload::completed);
        d.start(root.filePath("model.part"),s.payload.size(),"hash",{s.url("a"),s.url("b")},"both-slow",true);
        QTRY_COMPARE_WITH_TIMEOUT(selected.size(),3,5000);
        QCOMPARE(selected[0][0].toUrl(),s.url("a"));QCOMPARE(selected[1][0].toUrl(),s.url("b"));
        QCOMPARE(selected[2][0].toUrl(),s.url("a"));
        QTest::qWait(800);QCOMPARE(selected.size(),3);d.cancel();QCOMPARE(done.size(),1);
        const auto saved=ModelHttpDownload::savedBytes(root.filePath("model.part"),s.payload.size(),"hash");
        QVERIFY(saved>0);QCOMPARE(read(root.filePath("model.part")),s.payload.left(saved));
        bool continued=false;for(const auto &r:s.requests)if(r.path=="/b"&&r.begin>0)continued=true;
        QVERIFY(continued);
    }
    void appleCopyCancelAndResume() {
#ifdef Q_OS_UNIX
        QTemporaryDir root;const auto input=root.filePath("source"),out=root.filePath("part");
        QByteArray data(4*1024*1024,'a');QFile source(input);QVERIFY(source.open(QIODevice::ReadWrite));QCOMPARE(source.write(data),data.size());source.flush();
        std::atomic_bool cancelled=false;
        auto error=copyAssetResuming(source.handle(),out,data.size(),cancelled,[&](qint64 n){if(n>=1024*1024)cancelled=true;});
        QVERIFY(!error.isEmpty());QCOMPARE(QFileInfo(out).size(),qint64(1024*1024));cancelled=false;qint64 firstProgress=0;
        error=copyAssetResuming(source.handle(),out,data.size(),cancelled,[&](qint64 n){if(!firstProgress)firstProgress=n;});
        QVERIFY(error.isEmpty());QCOMPARE(firstProgress,qint64(2*1024*1024));QCOMPARE(read(out),data);
        QFile corrupt(out);QVERIFY(corrupt.open(QIODevice::WriteOnly));corrupt.write("bad");corrupt.close();
        error=copyAssetResuming(source.handle(),out,data.size(),cancelled);QVERIFY(error.isEmpty());QCOMPARE(read(out),data);
#else
        QSKIP("Apple asset-pack copying uses POSIX file descriptors");
#endif
    }
    void livePinnedVoiceSample() {
        const auto root=qEnvironmentVariable("MOSS_LIVE_VOICE_ROOT");
        if(root.isEmpty())QSKIP("Opt-in Ming partial transfer: set MOSS_LIVE_VOICE_ROOT");
        QVERIFY(QDir().mkpath(root));QFile catalog(QFINDTESTDATA("../config/models.json"));QVERIFY(catalog.open(QIODevice::ReadOnly));
        QJsonObject file;for(const auto &v:QJsonDocument::fromJson(catalog.readAll()).object()["files"].toArray())
            if(v.toObject()["path"]=="voice/ming-3.0-tts-0.5b-q8_0.gguf")file=v.toObject();
        QVERIFY(!file.isEmpty());
        const auto endpoint=qEnvironmentVariable("MOSS_LIVE_ENDPOINT","mirror");
        const auto host=endpoint=="official"?QString("https://huggingface.co"):QString("https://hf-mirror.com");
        const QUrl url(host+"/"+file["repo"].toString()+"/resolve/"+file["revision"].toString()+"/"+file["file"].toString());
        const auto path=root+"/voice-sample-"+endpoint+".part",sha=file["sha256"].toString();
        const auto bytes=file["bytes"].toInteger();qint64 saved=0;
        for(int run=0;run<2;++run) {
            ModelHttpDownload d;ModelHttpDownload::Policy p;p.connections=1;d.setPolicy(p);
            const auto target=ModelHttpDownload::savedBytes(path,bytes,sha)+64*1024;
            connect(&d,&ModelHttpDownload::progress,&d,[&](qint64 n){if(n>=target)d.cancel();});
            QSignalSpy done(&d,&ModelHttpDownload::completed);QElapsedTimer elapsed;elapsed.start();
            d.start(path,bytes,sha,{url},"voice-sample",false);
            QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,130000);
            QVERIFY2(done[0][0].toString().isEmpty(),qPrintable(done[0][0].toString()));QVERIFY(done[0][1].toBool());
            const auto next=ModelHttpDownload::savedBytes(path,bytes,sha);QVERIFY(next>saved);saved=next;
            qInfo()<<endpoint<<"run"<<run<<"saved"<<saved<<"elapsed ms"<<elapsed.elapsed();
        }
        QVERIFY(saved<bytes);QVERIFY(QFileInfo::exists(ModelHttpDownload::journalPath(path)));
    }
    void livePinnedFile() {
        const auto root=qEnvironmentVariable("MOSS_LIVE_DOWNLOAD_ROOT");
        if(root.isEmpty())QSKIP("Opt-in real network check: set MOSS_LIVE_DOWNLOAD_ROOT");
        QVERIFY(QDir().mkpath(root));QFile catalog(QFINDTESTDATA("../config/models.json"));QVERIFY(catalog.open(QIODevice::ReadOnly));
        QJsonObject file;for(const auto &v:QJsonDocument::fromJson(catalog.readAll()).object()["files"].toArray())
            if(v.toObject()["path"]=="tokenizer.json")file=v.toObject();
        QVERIFY(!file.isEmpty());const auto bytes=file["bytes"].toInteger();const auto sha=file["sha256"].toString();
        const auto relative="/"+file["repo"].toString()+"/resolve/"+file["revision"].toString()+"/"+file["file"].toString();
        QList<QUrl> urls{QUrl(file["modelscopeUrl"].toString()),QUrl("https://huggingface.co"+relative)};
        const auto endpoint=qEnvironmentVariable("MOSS_LIVE_ENDPOINT","auto");
        if(endpoint=="modelscope")urls={urls[0]};else if(endpoint=="official")urls={urls[1]};else if(endpoint=="mirror")urls={QUrl("https://hf-mirror.com"+relative)};
        const auto path=root+"/tokenizer-"+endpoint+".part";
        ModelHttpDownload first;QSignalSpy stopped(&first,&ModelHttpDownload::completed);
        connect(&first,&ModelHttpDownload::sourceSelected,this,[](QUrl url){qInfo()<<"Selected host:"<<url.host();});
        connect(&first,&ModelHttpDownload::progress,&first,[&](qint64 n){if(n>=1024*1024)first.cancel();});
        QElapsedTimer elapsed;elapsed.start();first.start(path,bytes,sha,urls,"live",urls.size()>1);
        QTRY_COMPARE_WITH_TIMEOUT(stopped.size(),1,90000);QVERIFY2(stopped[0][0].toString().isEmpty(),qPrintable(stopped[0][0].toString()));
        const auto saved=ModelHttpDownload::savedBytes(path,bytes,sha);QVERIFY(saved>0);qInfo()<<"Saved bytes before restart:"<<saved;
        ModelHttpDownload second;QSignalSpy done(&second,&ModelHttpDownload::completed);
        connect(&second,&ModelHttpDownload::sourceSelected,this,[](QUrl url){qInfo()<<"Resumed host:"<<url.host();});
        second.start(path,bytes,sha,urls,"live",urls.size()>1);QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,180000);
        QVERIFY2(done[0][0].toString().isEmpty(),qPrintable(done[0][0].toString()));
        QCOMPARE(QString::fromLatin1(QCryptographicHash::hash(read(path),QCryptographicHash::Sha256).toHex()),sha);
        qInfo()<<"Verified bytes:"<<bytes<<"elapsed ms:"<<elapsed.elapsed();
    }
};
QTEST_GUILESS_MAIN(HttpDownloadTests)
#include "model_http_download_tests.moc"
