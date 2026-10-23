#include "DiagnosticLog.h"
#include "InferenceDiagnostics.h"
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>
#include <thread>

class DiagnosticLogTests : public QObject {
    Q_OBJECT
    static QList<QJsonObject> entries(const QString &file) {
        QFile input(file); if (!input.open(QIODevice::ReadOnly)) return {};
        QList<QJsonObject> result;
        for (const auto &line : input.readAll().split('\n')) if (!line.isEmpty()) result.append(QJsonDocument::fromJson(line).object());
        return result;
    }
private slots:
    void shortSessionsReuseCapacity(){
        QTemporaryDir dir;
        for(int i=0;i<20;++i){DiagnosticLog log(dir.filePath("cache"));QVERIFY(log.record("info","session",{{"i",i}}));}
        QCOMPARE(QDir(dir.filePath("cache")).entryList({"*.jsonl"},QDir::Files).size(),1);
        DiagnosticLog log(dir.filePath("cache"));QString error;QVERIFY(log.exportTo(dir.filePath("all.log"),&error));
        QCOMPARE(entries(dir.filePath("all.log")).size(),21);
    }
    void retainsNewestWithinBudgetAcrossRestarts() {
        QTemporaryDir dir;
        const auto cache=dir.filePath("cache"), output=dir.filePath("export.log");
        {
            DiagnosticLog log(cache,1024,3);
            for(int i=0;i<15;++i) QVERIFY(log.record("info","sample",{{"index",i},{"message",QString(600,'x')}}));
        }
        DiagnosticLog reopened(cache,1024,3);
        QVERIFY(reopened.record("error","after-restart",{{"index",15}}));
        auto files=QDir(cache).entryInfoList({"*.jsonl"},QDir::Files);
        QVERIFY(files.size()<=3);
        qint64 total=0;for(const auto &file:files)total+=file.size();QVERIFY(total<=3072);
        QString error;QVERIFY2(reopened.exportTo(output,&error),qPrintable(error));
        const auto records=entries(output);QVERIFY(records.size()>1);
        bool newest=false;
        for(const auto &entry:records){QVERIFY(!entry.isEmpty());if(entry["event"]=="after-restart")newest=true;QVERIFY(entry["data"].toObject()["index"].toInt(-1)!=0);}
        QVERIFY(newest);
        auto after=QDir(cache).entryInfoList({"*.jsonl"},QDir::Files);
        QCOMPARE(after.size(),files.size());
        for(int i=0;i<files.size();++i)QCOMPARE(after[i].lastModified(),files[i].lastModified());
    }
    void boundsHugeRecordsAndConcurrentWriters() {
        QTemporaryDir dir;DiagnosticLog log(dir.filePath("cache"),4096,4);
        QVERIFY(log.record("info","huge",{{"values",QJsonArray{QString(200000,'x'),QString(200000,'y')}}}));
        bool ok1=true,ok2=true;
        std::thread a([&]{for(int i=0;i<50;++i)ok1 &= log.record("info","thread-a",{{"i",i}});});
        std::thread b([&]{for(int i=0;i<50;++i)ok2 &= log.record("error","thread-b",{{"i",i}});});
        a.join();b.join();QVERIFY(ok1);QVERIFY(ok2);
        QString error;QVERIFY(log.exportTo(dir.filePath("export.log"),&error));
        for(const auto &entry:entries(dir.filePath("export.log")))QVERIFY(!entry.isEmpty());
        qint64 total=0;for(const auto &file:QDir(dir.filePath("cache")).entryInfoList({"*.jsonl"},QDir::Files))total+=file.size();QVERIFY(total<=16384);
    }
    void redactsBeforePersistenceAndReportsExportFailures() {
        QTemporaryDir dir;DiagnosticLog log(dir.filePath("cache"));
        QVERIFY(log.record("error","fixture",{{"password","hidden-password"},{"prompt","private-prompt"},
            {"message","CUDA failure C:\\Users\\Alice\\secret.png token=hidden-token"},
            {"url","https://example.test/?token=secret"},{"home",QDir::homePath()+"/private.txt"}}));
        QString error;
        QVERIFY(!log.exportTo(dir.filePath("missing/result.log"),&error));QVERIFY(!error.isEmpty());
        QVERIFY(!log.exportTo(dir.filePath("cache/diagnostic-overwrite.jsonl"),&error));
        QVERIFY(log.exportTo(dir.filePath("result.log"),&error));
        QFile file(dir.filePath("result.log"));QVERIFY(file.open(QIODevice::ReadOnly));const auto bytes=file.readAll();
        for(const auto &secret:QList<QByteArray>{"hidden-password","private-prompt","hidden-token","Alice","private.txt","example.test"})QVERIFY2(!bytes.contains(secret),secret.constData());
    }
    void detectionDoesNotConfuseAvailabilityFailureOrOffloadWithGpu() {
        QTemporaryDir dir;DiagnosticLog log(dir.filePath("cache"));InferenceDiagnostics job(&log);
        job.begin("image","sd-cli",false,{{"prompt","never-export"},{"offloadToCpu",true}});
        job.consume("Found CUDA device 0: GPU\nfailed to initialize CUDA backend\nCoreMLExecutionProvider available\n");
        QCOMPARE(job.summary()["gpu"].toString(),QString("unknown"));
        job.consume("Using CPU backend\n");QCOMPARE(job.summary()["gpu"].toString(),QString("no"));
        job.consume("diffusion compute buffer size: 24.00 MB(VRAM) on CUDA0 (peak across 1 segment)\n");
        QCOMPARE(job.summary()["gpu"].toString(),QString("mixed"));
        job.finish(false,false,7,false,"failed");
        job.begin("image","sd-cli",false,{});QCOMPARE(job.summary()["gpu"].toString(),QString("unknown"));
        job.consume("{\"event\":\"backend\",\"backend\":\"CoreML\",\"gpu\":\"unknown\"}\n");
        QCOMPARE(job.summary()["gpu"].toString(),QString("unknown"));
        job.finish(false,true,15,false,"");
        QString error;QVERIFY(log.exportTo(dir.filePath("result.log"),&error));
        bool cancelled=false;for(const auto &entry:entries(dir.filePath("result.log")))if(entry["data"].toObject()["cancelled"].toBool())cancelled=true;
        QVERIFY(cancelled);
    }
    void preservesSplitUtf8FinalErrorAndStageTimings() {
        QTemporaryDir dir;DiagnosticLog log(dir.filePath("cache"));InferenceDiagnostics job(&log);
        job.begin("voice","moss-voice",false,{});
        const auto bytes=QString::fromUtf8("{\"event\":\"backend\",\"backend\":\"metal\",\"gpu\":\"yes\",\"component\":\"voice\"}\r{\"event\":\"progress\",\"stage\":\"voice-synthesis\",\"done\":0,\"total\":1}\n{\"event\":\"error\",\"message\":\"显存不足\"}").toUtf8();
        for(char byte:bytes)job.consume(QByteArray(1,byte));
        job.finish(false,false,1,false,"failed",{{"elapsedSeconds",1.5},{"timingsOverlap",true}});
        QString error;QVERIFY(log.exportTo(dir.filePath("result.log"),&error));
        bool failure=false,stage=false,finish=false;
        for(const auto &entry:entries(dir.filePath("result.log"))){
            const auto data=entry["data"].toObject();
            if(entry["event"]=="worker-error")failure=data["message"]=="显存不足";
            if(entry["event"]=="stage-finished")stage=data["wallMs"].toDouble(-1)>=0;
            if(entry["event"]=="inference-finished"){finish=true;QCOMPARE(data["gpu"].toString(),QString("yes"));QCOMPARE(data["performance"].toObject()["elapsedSeconds"].toDouble(),1.5);}
        }
        QVERIFY(failure);QVERIFY(stage);QVERIFY(finish);
    }
    void realWorkerExitAndNoNewlineAreCaptured() {
        QTemporaryDir dir;DiagnosticLog log(dir.filePath("cache"));InferenceDiagnostics job(&log);
        QProcess process;process.setProcessChannelMode(QProcess::MergedChannels);
        job.begin("fixture","diagnostic_log_tests",false,{});
        process.start(QCoreApplication::applicationFilePath(),{"--diagnostic-worker-fixture"});
        QVERIFY(process.waitForStarted());QVERIFY(process.waitForFinished());
        job.consume(process.readAllStandardOutput());
        job.finish(false,false,process.exitCode(),process.exitStatus()==QProcess::CrashExit,"fixture-error");
        QCOMPARE(process.exitCode(),7);QCOMPARE(job.summary()["gpu"].toString(),QString("yes"));
        QString error;QVERIFY(log.exportTo(dir.filePath("result.log"),&error));
        bool found=false;for(const auto &entry:entries(dir.filePath("result.log")))if(entry["event"]=="worker-error")found=true;
        QVERIFY(found);
        job.begin("fixture","missing-worker",false,{});
        process.start(dir.filePath("does-not-exist"),{});QVERIFY(!process.waitForStarted());
        job.processError(int(process.error()),process.errorString());job.finish(false,false,-1,false,process.errorString());
        QVERIFY(log.exportTo(dir.filePath("result.log"),&error));
        found=false;for(const auto &entry:entries(dir.filePath("result.log")))if(entry["event"]=="process-error")found=true;
        QVERIFY(found);
    }
};
int main(int argc,char **argv){
    QCoreApplication app(argc,argv);
    if(app.arguments().contains("--diagnostic-worker-fixture")){
        std::fputs("Using CUDA0 backend\n{\"event\":\"error\",\"message\":\"fixture final error\"}",stderr);return 7;
    }
    DiagnosticLogTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "diagnostic_log_tests.moc"
