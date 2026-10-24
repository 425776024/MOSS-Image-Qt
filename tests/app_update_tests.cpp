#include "AppUpdate.h"
#include <QtTest>
#include <QTimer>
#include <QUrlQuery>
#include <cstring>

class Reply final:public QNetworkReply {
    QByteArray m_data;
    qint64 m_at=0;
public:
    Reply(const QNetworkRequest &request,QByteArray bytes,bool failed,QObject *parent):QNetworkReply(parent),m_data(std::move(bytes)) {
        setRequest(request);setUrl(request.url());setHeader(QNetworkRequest::ContentLengthHeader,1024);open(QIODevice::ReadOnly|QIODevice::Unbuffered);
        QTimer::singleShot(0,this,[this,failed]{
            if(failed)setError(QNetworkReply::ContentAccessDenied,"HTTP 403");
            emit readyRead();setFinished(true);emit finished();
        });
    }
    void abort() override {setError(QNetworkReply::OperationCanceledError,"cancelled");}
    qint64 bytesAvailable() const override {return m_data.size()-m_at+QNetworkReply::bytesAvailable();}
    bool isSequential() const override {return true;}
protected:
    qint64 readData(char *data,qint64 max) override {const auto n=qMin(max,m_data.size()-m_at);if(!n)return -1;std::memcpy(data,m_data.constData()+m_at,n);m_at+=n;return n;}
};
class Network final:public QNetworkAccessManager {
public:
    QByteArray response="[]";bool failed=false,failedApi=false;QList<QUrl> requests;QMap<QString,QByteArray> responses;
protected:
    QNetworkReply *createRequest(Operation,const QNetworkRequest &request,QIODevice *) override {
        requests<<request.url();return new Reply(request,responses.value(request.url().path(),response),failed || (failedApi && request.url().host()=="api.github.com"),this);
    }
};
class UpdateTests:public QObject {
    Q_OBJECT
    QJsonObject release(QString platform,QString time="2026-10-24T00:00:00Z",QString version="0.1.0") {
        const QString url="https://github.com/425776024/MOSS-Image-Qt/releases/";
        const auto name=platform=="windows"?"XAvatar-"+version+"-Windows-x64-Setup.exe":"Moss-Studio-"+version+"-macOS-arm64-notarized.app.zip";
        return {{"tag_name",platform+"-build-123"},{"published_at",time},{"created_at",time},{"prerelease",true},
            {"target_commitish","remote-commit"},{"html_url",url+"tag/build-123"},{"body","What's new"},
            {"assets",QJsonArray{QJsonObject{{"name",name},{"size",1024},{"digest","sha256:"+QString(64,'a')},{"browser_download_url",url+"download/build-123/"+name}}}}};
    }
private slots:
    void platformAndPreviewSelection() {
        const QJsonArray releases{release("windows","2026-10-24T02:00:00Z"),release("macos"),release("macos","2026-10-23T00:00:00Z")};
        const auto mac=AppUpdate::selectRelease(releases,"macos"),win=AppUpdate::selectRelease(releases,"windows");
        QCOMPARE(mac["publishedAt"].toString(),QString("2026-10-24T00:00:00Z"));QVERIFY(mac["preview"].toBool());
        QVERIFY(mac["downloadUrl"].toString().endsWith("notarized.app.zip"));QVERIFY(win["downloadUrl"].toString().endsWith("Setup.exe"));
    }
    void ignoresDraftUnsignedAndUntrustedPackages() {
        auto draft=release("macos");draft["draft"]=true;
        auto untrusted=release("macos");untrusted["html_url"]="https://example.com/releases/tag/x";
        auto asset=release("macos")["assets"].toArray()[0].toObject();asset["digest"]="";
        auto unsignedRelease=release("macos");unsignedRelease["assets"]=QJsonArray{asset};
        QVERIFY(AppUpdate::selectRelease({draft,untrusted,unsignedRelease},"macos").isEmpty());
        asset["digest"]="sha256:"+QString(64,'a');asset["browser_download_url"]="https://github.com/other/repo/releases/download/x/app.zip";
        unsignedRelease["assets"]=QJsonArray{asset};QVERIFY(AppUpdate::selectRelease({unsignedRelease},"macos").isEmpty());
    }
    void versionAndSameVersionBuildComparison() {
        auto row=AppUpdate::selectRelease({release("macos")},"macos");
        QVERIFY(!AppUpdate::newer(row,"0.1.0","remote-commit","2026-01-01T00:00:00Z"));
        QVERIFY(AppUpdate::newer(row,"0.1.0","old-commit","2026-10-23T00:00:00Z"));
        QVERIFY(!AppUpdate::newer(row,"0.1.0","new-commit","2026-10-25T00:00:00Z"));
        row["version"]="0.10.0";QVERIFY(AppUpdate::newer(row,"0.9.0",{},{}));
        QVERIFY(!AppUpdate::newer(row,"1.0.0",{},{}));
    }
    void officialEndpoints() {
        QCOMPARE(AppUpdate::endpoint(false).host(),QString("api.github.com"));
        const auto apple=AppUpdate::endpoint(true,"CN");QCOMPARE(apple.host(),QString("itunes.apple.com"));
        QCOMPARE(QUrlQuery(apple).queryItemValue("country"),QString("cn"));QCOMPARE(QUrlQuery(apple).queryItemValue("id"),QString("6825373745"));
    }
    void errorsAreRetryableAndEmptyIsNotCurrent() {
        Network network;AppUpdate update(nullptr,&network);network.failed=true;
        update.check();update.check();QCOMPARE(network.requests.size(),1);
        QTRY_COMPARE(update.state(),update.appleStore()?QString("error"):QString("rateLimited"));
        network.failed=false;network.response="not json";update.check();QTRY_COMPARE(update.state(),QString("error"));
        network.response=update.appleStore()?"{\"resultCount\":0,\"results\":[]}":"[]";
        update.check();QTRY_COMPARE(update.state(),QString("notPublished"));QVERIFY(update.latest().isEmpty());QVERIFY(!update.canInstall());
    }
    void checksPublishedVersion() {
        QCoreApplication::setApplicationVersion("0.1.0");Network network;AppUpdate update(nullptr,&network);
        if(update.appleStore())network.response=R"({"results":[{"bundleId":"ai.ffclip.studio","trackId":6825373745,"version":"0.2.0","releaseNotes":"New version"}]})";
        else network.response=QJsonDocument(QJsonArray{release("macos", "2026-10-24T00:00:00Z","0.2.0"),release("windows","2026-10-24T00:00:00Z","0.2.0")}).toJson();
        update.check();QTRY_COMPARE(update.state(),QString("available"));QCOMPARE(update.latest()["version"].toString(),QString("0.2.0"));
        if(update.appleStore())QVERIFY(!update.canInstall());
    }
    void relaunchTokenCannotWriteOutsideTransaction() {
        QVERIFY(!AppUpdate::acknowledgeLaunch("../unrelated"));QVERIFY(!AppUpdate::acknowledgeLaunch(QString(32,'a')));
    }
    void cancelAndRejectCorruptDownload() {
#if defined(Q_OS_MACOS) && !defined(MOSS_APP_STORE)
        QTemporaryDir root;const auto target=root.filePath("XAvatar.app");
        QVERIFY(QDir().mkpath(target+"/Contents/Frameworks/QtCore.framework"));
        QFile sentinel(root.filePath("user-project.txt"));QVERIFY(sentinel.open(QIODevice::WriteOnly));sentinel.write("keep");sentinel.close();
        Network network;AppUpdate update(nullptr,&network,target);QSignalSpy restart(&update,&AppUpdate::restartRequested);
        network.response=QJsonDocument(QJsonArray{release("macos","2026-10-24T00:00:00Z","0.2.0")}).toJson();
        update.check();QTRY_COMPARE(update.state(),QString("available"));QVERIFY(update.canInstall());
        network.response=QByteArray(1024,'x');update.install();QCOMPARE(update.state(),QString("downloading"));
        update.cancel();QCOMPARE(update.state(),QString("available"));
        update.install();QTRY_COMPARE(update.state(),QString("verificationFailed"));
        QVERIFY(QFileInfo::exists(target));QVERIFY(QFileInfo::exists(sentinel.fileName()));QCOMPARE(restart.size(),0);
#endif
    }
    void releaseFeedFallbackKeepsDigestAndPlatform() {
        const QByteArray feed=R"(<feed xmlns="http://www.w3.org/2005/Atom"><entry><updated>2026-10-24T00:00:00Z</updated><link href="https://github.com/425776024/MOSS-Image-Qt/releases/tag/macos-build-123"/><content type="html">&lt;p&gt;New macOS build&lt;/p&gt;</content></entry><entry><updated>2026-10-24T01:00:00Z</updated><link href="https://github.com/425776024/MOSS-Image-Qt/releases/tag/windows-build-123"/><content type="html">Windows</content></entry></feed>)";
        QCOMPARE(AppUpdate::feedRelease(feed,"macos")["tag"].toString(),QString("macos-build-123"));
        QVERIFY(AppUpdate::feedRelease("<invalid>","macos").isEmpty());
        const QByteArray sums=QByteArray(64,'a')+"  Moss-Studio-0.2.0-macOS-arm64-notarized.app.zip\n";
        QCOMPARE(AppUpdate::checksumAsset(sums,"macos","macos-build-123")["version"].toString(),QString("0.2.0"));
        QVERIFY(AppUpdate::checksumAsset(sums,"windows","windows-build-123").isEmpty());
        QVERIFY(AppUpdate::checksumAsset(sums,"macos","../outside").isEmpty());
#ifndef MOSS_APP_STORE
        Network network;network.failedApi=true;network.responses["/425776024/MOSS-Image-Qt/releases.atom"]=feed;
#ifdef Q_OS_WIN
        const QString tag="windows-build-123";
        network.responses["/425776024/MOSS-Image-Qt/releases/download/"+tag+"/SHA256SUMS-windows-x64.txt"]=QByteArray(64,'a')+"  XAvatar-0.2.0-Windows-x64-Setup.exe\n";
#else
        const QString tag="macos-build-123";
        network.responses["/425776024/MOSS-Image-Qt/releases/download/"+tag+"/SHA256SUMS.txt"]=sums;
#endif
        network.responses["/425776024/MOSS-Image-Qt/releases/download/"+tag+"/build-source.json"]=R"({"commit":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","builtAt":"2026-10-24T00:00:00Z"})";
        AppUpdate update(nullptr,&network);QCoreApplication::setApplicationVersion("0.1.0");update.check();
        QTRY_COMPARE(update.state(),QString("available"));QCOMPARE(update.latest()["sha256"].toString(),QString(64,'a'));
        QCOMPARE(update.latest()["bytes"].toLongLong(),1024LL);QCOMPARE(network.requests.size(),5);
#endif
    }
};
QTEST_GUILESS_MAIN(UpdateTests)
#include "app_update_tests.moc"
