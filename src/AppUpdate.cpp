#include "AppUpdate.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLocale>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTimer>
#include <QUrlQuery>
#include <QUuid>
#include <QVersionNumber>
#include <QXmlStreamReader>
#include <QTextDocument>

#ifndef MOSS_BUILD_COMMIT
#define MOSS_BUILD_COMMIT ""
#define MOSS_BUILD_TIMESTAMP ""
#endif
#ifndef MOSS_BUILD_NUMBER
#define MOSS_BUILD_NUMBER "1"
#endif
namespace {
const QString repo="https://github.com/425776024/MOSS-Image-Qt";
bool releaseUrl(const QUrl &url) {
    return url.scheme()=="https" && url.host()=="github.com" && url.userInfo().isEmpty()
        && url.path().startsWith("/425776024/MOSS-Image-Qt/releases/");
}
QJsonObject readObject(const QString &path) {
    QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};
    return QJsonDocument::fromJson(file.read(2*1024*1024)).object();
}
bool writeObject(const QString &path,const QJsonObject &object) {
    QSaveFile file(path);return file.open(QIODevice::WriteOnly)
        && file.write(QJsonDocument(object).toJson())>0 && file.commit();
}
QString platform() {
#ifdef Q_OS_WIN
    return "windows";
#else
    return "macos";
#endif
}
}
AppUpdate::AppUpdate(QObject *parent,QNetworkAccessManager *network,QString installation):QObject(parent),m_installation(std::move(installation)),m_network(network?network:new QNetworkAccessManager(this)) {}
bool AppUpdate::appleStore() const {
#ifdef MOSS_APP_STORE
    return true;
#else
    return false;
#endif
}
QString AppUpdate::currentVersion() const {
    const auto build=appleStore()?QString(MOSS_BUILD_NUMBER):QString(MOSS_BUILD_COMMIT).left(7);
    return QCoreApplication::applicationVersion()+(build.isEmpty()?QString():" ("+build+")");
}
QUrl AppUpdate::endpoint(bool store,const QString &country) {
    if(!store)return QUrl("https://api.github.com/repos/425776024/MOSS-Image-Qt/releases?per_page=100");
    QUrl url("https://itunes.apple.com/lookup");QUrlQuery query;
    query.addQueryItem("id","6825373745");query.addQueryItem("entity","macSoftware");
    query.addQueryItem("country",country.isEmpty()?"us":country.toLower());url.setQuery(query);return url;
}
QString AppUpdate::updateRoot() {return QStandardPaths::writableLocation(QStandardPaths::CacheLocation)+"/app-updates";}
QString AppUpdate::installRoot() const {
    if(!m_installation.isEmpty())return m_installation;
#ifdef Q_OS_MACOS
    return QDir::cleanPath(QCoreApplication::applicationDirPath()+"/../..");
#else
    return QCoreApplication::applicationDirPath();
#endif
}
bool AppUpdate::canInstall() const {
    if(appleStore() || m_latest.isEmpty() || m_latest["sha256"].toString().isEmpty())return false;
    const auto root=installRoot();
#ifdef Q_OS_MACOS
    return root.endsWith(".app") && !root.startsWith("/Volumes/") && !root.contains("/AppTranslocation/")
        && QFileInfo(root+"/Contents/Frameworks/QtCore.framework").exists()
        && !QFileInfo(root+"/Contents/Resources/models").exists()
        && QFileInfo(QFileInfo(root).absolutePath()).isWritable();
#elif defined(Q_OS_WIN)
    return QFileInfo(root+"/update-files.json").isFile() && QFileInfo(root).isWritable();
#else
    return false;
#endif
}
void AppUpdate::setState(const QString &state) {m_state=state;emit changed();}
void AppUpdate::fail(const QString &code) {
    if(m_package){m_package->cancelWriting();m_package.reset();}
    // This directory was made by this operation, never the installation/data root.
    if(!m_work.isEmpty())QDir(m_work).removeRecursively();
    m_work.clear();setState(code);
}
void AppUpdate::fetch(const QUrl &url,std::function<void(QByteArray)> done,std::function<void()> failed) {
    QNetworkRequest request(url);request.setTransferTimeout(20000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setRawHeader("Accept","application/json");request.setRawHeader("User-Agent","XAvatar-Update/"+QCoreApplication::applicationVersion().toUtf8());
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,QNetworkRequest::AlwaysNetwork);
    auto reply=m_network->get(request);m_reply=reply;
    connect(reply,&QNetworkReply::readyRead,this,[reply]{if(reply->bytesAvailable()>4*1024*1024)reply->abort();});
    connect(reply,&QNetworkReply::finished,this,[this,reply,done=std::move(done),failed=std::move(failed)]{
        const auto bytes=reply->readAll();const bool ok=reply->error()==QNetworkReply::NoError && bytes.size()<=4*1024*1024;
        reply->deleteLater();m_reply=nullptr;if(!ok){if(failed)failed();else fail();return;}done(bytes);
    });
}
QJsonObject AppUpdate::selectRelease(const QJsonArray &releases,const QString &platform) {
    QJsonObject selected;
    const QRegularExpression package(platform=="windows" ? R"(^XAvatar-([0-9]+\.[0-9]+\.[0-9]+)-Windows-x64-Setup\.exe$)"
        : R"(^Moss-Studio-([0-9]+\.[0-9]+\.[0-9]+)-macOS-arm64-notarized\.app\.zip$)");
    for(const auto &value:releases){
        const auto row=value.toObject();if(row["draft"].toBool() || !releaseUrl(QUrl(row["html_url"].toString())))continue;
        const auto published=QDateTime::fromString(row["published_at"].toString(),Qt::ISODate);
        if(!published.isValid())continue;
        if(!selected.isEmpty() && published<=QDateTime::fromString(selected["publishedAt"].toString(),Qt::ISODate))continue;
        for(const auto &value:row["assets"].toArray()){
            const auto asset=value.toObject();const auto match=package.match(asset["name"].toString());
            if(!match.hasMatch() || !releaseUrl(QUrl(asset["browser_download_url"].toString())))continue;
            const auto digest=asset["digest"].toString();
            if(!QRegularExpression("^sha256:[0-9a-f]{64}$").match(digest).hasMatch() || asset["size"].toDouble()<=0)continue;
            selected={{"version",match.captured(1)},{"tag",row["tag_name"]},{"commit",row["target_commitish"]},
                {"createdAt",row["created_at"]},{"publishedAt",row["published_at"]},{"notes",row["body"]},
                {"url",row["html_url"]},{"downloadUrl",asset["browser_download_url"]},{"sha256",digest.mid(7)},
                {"bytes",asset["size"]},{"preview",row["prerelease"]}};break;
        }
    }return selected;
}
bool AppUpdate::newer(const QJsonObject &row,const QString &version,const QString &commit,const QString &builtAt) {
    const auto comparison=QVersionNumber::compare(QVersionNumber::fromString(row["version"].toString()),QVersionNumber::fromString(version));
    if(comparison!=0)return comparison>0;
    if(commit.isEmpty() || row["commit"].toString()==commit)return false;
    const auto remote=QDateTime::fromString(row["createdAt"].toString(),Qt::ISODate);
    const auto local=QDateTime::fromString(builtAt,Qt::ISODate);
    return remote.isValid() && local.isValid() && remote>local;
}
void AppUpdate::checkCommunity(int page) {
    auto url=endpoint(false);if(page>1){auto query=QUrlQuery(url);query.addQueryItem("page",QString::number(page));url.setQuery(query);}
    fetch(url,[this,page](QByteArray bytes){
        QJsonParseError error;const auto document=QJsonDocument::fromJson(bytes,&error);
        if(error.error!=QJsonParseError::NoError || !document.isArray()){fail();return;}
        m_latest=selectRelease(document.array(),platform());
        if(m_latest.isEmpty() && document.array().size()==100 && page<10){checkCommunity(page+1);return;}
        if(m_latest.isEmpty()){setState("notPublished");return;}
        finishCommunityCheck();
    },[this]{checkFeed();});
}
void AppUpdate::finishCommunityCheck() {
    if(QString(MOSS_BUILD_COMMIT).isEmpty() && m_latest["version"]==QCoreApplication::applicationVersion())setState("unconfirmed");
    else setState(newer(m_latest,QCoreApplication::applicationVersion(),MOSS_BUILD_COMMIT,MOSS_BUILD_TIMESTAMP)?"available":"current");
}
QJsonObject AppUpdate::feedRelease(const QByteArray &data,const QString &platform) {
    QXmlStreamReader xml(data);QJsonObject selected;
    while(!xml.atEnd()){
        xml.readNext();if(!xml.isStartElement() || xml.name()!=u"entry")continue;
        QString url,time,html;
        while(xml.readNextStartElement()){
            if(xml.name()==u"link")url=xml.attributes().value("href").toString();
            else if(xml.name()==u"updated"){time=xml.readElementText();continue;}
            else if(xml.name()==u"content"){html=xml.readElementText();continue;}
            xml.skipCurrentElement();
        }
        if(!releaseUrl(QUrl(url)) || !QDateTime::fromString(time,Qt::ISODate).isValid())continue;
        const auto tag=QUrl(url).path().section('/',-1);
        if(!QRegularExpression("^("+platform+"-[A-Za-z0-9._-]+|v[0-9][A-Za-z0-9._-]*)$").match(tag).hasMatch())continue;
        if(!selected.isEmpty() && time<=selected["publishedAt"].toString())continue;
        QTextDocument text;text.setHtml(html);
        selected={{"url",url},{"tag",tag},{"publishedAt",time},{"notes",text.toPlainText()}};
    }return xml.hasError()?QJsonObject():selected;
}
QJsonObject AppUpdate::checksumAsset(const QByteArray &data,const QString &platform,const QString &tag) {
    if(!QRegularExpression("^[A-Za-z0-9._-]+$").match(tag).hasMatch())return {};
    const QString file=platform=="windows"?R"(XAvatar-([0-9]+\.[0-9]+\.[0-9]+)-Windows-x64-Setup\.exe)":R"(Moss-Studio-([0-9]+\.[0-9]+\.[0-9]+)-macOS-arm64-notarized\.app\.zip)";
    const QRegularExpression pattern("^([0-9a-f]{64})  ("+file+")$");
    for(const auto &line:QString::fromUtf8(data).split('\n')){
        const auto match=pattern.match(line.trimmed());if(!match.hasMatch())continue;
        return {{"sha256",match.captured(1)},{"version",match.captured(3)},{"downloadUrl",repo+"/releases/download/"+tag+"/"+match.captured(2)}};
    }return {};
}
void AppUpdate::checkFeed() {
    // GitHub's public release feed and our published checksum/provenance files
    // remain accessible when its unauthenticated REST API is rate limited.
    fetch(QUrl(repo+"/releases.atom"),[this](QByteArray bytes){
        m_latest=feedRelease(bytes,platform());if(m_latest.isEmpty()){fail("rateLimited");return;}
        const auto base=repo+"/releases/download/"+m_latest["tag"].toString()+"/";
        fetch(QUrl(base+(platform()=="windows"?"SHA256SUMS-windows-x64.txt":"SHA256SUMS.txt")),[this,base](QByteArray bytes){
            const auto asset=checksumAsset(bytes,platform(),m_latest["tag"].toString());
            if(asset.isEmpty()){fail("rateLimited");return;}
            for(auto it=asset.begin();it!=asset.end();++it)m_latest[it.key()]=it.value();
            fetch(QUrl(base+"build-source.json"),[this](QByteArray bytes){
                const auto source=QJsonDocument::fromJson(bytes).object();
                if(!QRegularExpression("^[0-9a-f]{40}$").match(source["commit"].toString()).hasMatch()) {fail("rateLimited");return;}
                m_latest["commit"]=source["commit"];m_latest["createdAt"]=source["builtAt"];
                QNetworkRequest request(QUrl(m_latest["downloadUrl"].toString()));request.setTransferTimeout(20000);
                request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::NoLessSafeRedirectPolicy);
                auto reply=m_network->head(request);m_reply=reply;
                connect(reply,&QNetworkReply::finished,this,[this,reply]{
                    const auto size=reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
                    const bool ok=reply->error()==QNetworkReply::NoError && size>0;
                    reply->deleteLater();m_reply=nullptr;
                    if(!ok){fail("rateLimited");return;}m_latest["bytes"]=size;finishCommunityCheck();
                });
            },[this]{fail("rateLimited");});
        },[this]{fail("rateLimited");});
    },[this]{fail("rateLimited");});
}
void AppUpdate::check() {
    if(m_state=="checking" || m_state=="downloading" || m_state=="verifying" || m_state=="installing")return;
    m_latest={};setState("checking");
    if(!appleStore()){checkCommunity();return;}
    auto country=QLocale::territoryToCode(QLocale().territory());if(country.size()!=2)country="us";
    fetch(endpoint(true,country),[this](QByteArray bytes){
        QJsonParseError error;const auto document=QJsonDocument::fromJson(bytes,&error);
        if(error.error!=QJsonParseError::NoError || !document.isObject() || !document.object()["results"].isArray()){fail();return;}
        for(const auto &value:document.object()["results"].toArray()){
            const auto row=value.toObject();if(row["bundleId"]!="ai.ffclip.studio" || row["trackId"].toDouble()!=6825373745.)continue;
            if(QVersionNumber::fromString(row["version"].toString()).isNull()){fail();return;}
            m_latest={{"version",row["version"]},{"notes",row["releaseNotes"]},{"publishedAt",row["currentVersionReleaseDate"]}};
            setState(newer(m_latest,QCoreApplication::applicationVersion(),{}, {})?"available":"current");return;
        }setState("notPublished");
    });
}
void AppUpdate::cancel() {
    if(m_state!="downloading")return;
    if(m_reply){disconnect(m_reply,nullptr,this,nullptr);m_reply->abort();m_reply->deleteLater();m_reply=nullptr;}
    fail("available");
}
void AppUpdate::openStore(bool testFlight) {
    if(!appleStore()){
        const auto url=QUrl(m_latest["url"].toString());QDesktopServices::openUrl(releaseUrl(url)?url:QUrl(repo+"/releases"));return;
    }
    if(testFlight){if(!QDesktopServices::openUrl(QUrl("itms-beta://")))QDesktopServices::openUrl(QUrl("https://testflight.apple.com/"));}
    else QDesktopServices::openUrl(QUrl("macappstore://itunes.apple.com/app/id6825373745"));
}
void AppUpdate::install() {
#ifndef MOSS_APP_STORE
    if(m_state!="available" || !canInstall() || (canRestart && !canRestart()))return;
    m_token=QUuid::createUuid().toString(QUuid::Id128);m_work=updateRoot()+"/"+m_token;
    if(!QDir().mkpath(m_work)){fail();return;}
    // Keep the active transaction isolated from settings, models and projects.
    if(QStorageInfo(installRoot()).bytesAvailable()<m_latest["bytes"].toDouble()*4+512*1024*1024){fail("diskFull");return;}
    m_packagePath=m_work+(platform()=="windows"?"/package.exe":"/package.zip");
    m_package=std::make_unique<QSaveFile>(m_packagePath);
    if(!m_package->open(QIODevice::WriteOnly)){fail();return;}
    m_hash.reset();m_received=0;m_progress=0;setState("downloading");
    QNetworkRequest request(QUrl(m_latest["downloadUrl"].toString()));request.setTransferTimeout(30000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setRawHeader("User-Agent","XAvatar-Update");auto reply=m_network->get(request);m_reply=reply;
    connect(reply,&QNetworkReply::readyRead,this,[this,reply]{
        const auto bytes=reply->readAll();m_received+=bytes.size();
        if(!m_package || m_received>m_latest["bytes"].toDouble() || m_package->write(bytes)!=bytes.size()){reply->abort();return;}
        m_hash.addData(bytes);m_progress=m_received/m_latest["bytes"].toDouble();emit changed();
    });
    connect(reply,&QNetworkReply::finished,this,[this,reply]{
        const bool ok=reply->error()==QNetworkReply::NoError && m_received==m_latest["bytes"].toDouble()
            && m_hash.result().toHex()==m_latest["sha256"].toString().toLatin1();
        reply->deleteLater();m_reply=nullptr;
        if(!ok || !m_package || !m_package->commit()){fail("verificationFailed");return;}
        m_package.reset();setState("verifying");prepareInstall();
    });
#endif
}
void AppUpdate::run(const QString &program,const QStringList &arguments,std::function<void(bool,QByteArray)> done) {
    auto process=new QProcess(this);process->setProcessChannelMode(QProcess::MergedChannels);
    auto timer=new QTimer(process);timer->setSingleShot(true);timer->start(180000);
    connect(timer,&QTimer::timeout,process,[process]{process->kill();});
    auto finished=std::make_shared<bool>(false);
    auto complete=[process,finished,done=std::move(done)](bool ok){if(*finished)return;*finished=true;auto output=process->readAll();process->deleteLater();done(ok,output);};
    connect(process,&QProcess::errorOccurred,this,[complete](QProcess::ProcessError error){if(error==QProcess::FailedToStart)complete(false);});
    connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[complete](int code,QProcess::ExitStatus status){complete(code==0 && status==QProcess::NormalExit);});
    process->start(program,arguments);
}
void AppUpdate::prepareInstall() {
#if defined(Q_OS_MACOS) && !defined(MOSS_APP_STORE)
    const auto stage=m_work+"/staged";QDir().mkpath(stage);
    run("/usr/bin/ditto",{"-x","-k",m_packagePath,stage},[this,stage](bool ok,QByteArray){
        const auto apps=QDir(stage).entryList({"*.app"},QDir::Dirs|QDir::NoDotAndDotDot|QDir::NoSymLinks);
        if(!ok || apps.size()!=1){fail("verificationFailed");return;}
        const auto app=stage+"/"+apps.first();
        run("/usr/bin/codesign",{"--verify","--deep","--strict",app},[this,app](bool ok,QByteArray){
            if(!ok){fail("verificationFailed");return;}
            run("/usr/bin/codesign",{"-d","--verbose=4",installRoot()},[this,app](bool ok,QByteArray output){
                const auto team=QRegularExpression("(?:^|\n)TeamIdentifier=([A-Z0-9]{10})(?:\n|$)").match(QString::fromUtf8(output)).captured(1);
                if(!ok || team.isEmpty()){fail("verificationFailed");return;}
                run("/usr/bin/codesign",{"--verify","-R","identifier \"ai.ffclip.studio\" and anchor apple generic and certificate leaf[subject.OU] = \""+team+"\"",app},[this,app](bool ok,QByteArray){
                    if(!ok){fail("verificationFailed");return;}
                    run("/usr/sbin/spctl",{"--assess","--type","execute",app},[this,app](bool ok,QByteArray){if(ok)launchHelper(app);else fail("verificationFailed");});
                });
            });
        });
    });
#elif defined(Q_OS_WIN) && !defined(MOSS_APP_STORE)
    launchHelper();
#else
    fail();
#endif
}
void AppUpdate::launchHelper(const QString &staged) {
#ifndef MOSS_APP_STORE
    if(canRestart && !canRestart()){fail("busy");return;}
    const auto script=m_work+(platform()=="windows"?"/update.ps1":"/update.sh");
    if(!QFile::copy(platform()=="windows"?":/updates/scripts/update_windows.ps1":":/updates/scripts/update_macos.sh",script)
        || !writeObject(m_work+"/pending.json",{{"target",installRoot()},{"token",m_token}})){fail();return;}
    QString program;QStringList arguments;
#ifdef Q_OS_WIN
    program=QStandardPaths::findExecutable("powershell.exe",{qEnvironmentVariable("SystemRoot")+"/System32/WindowsPowerShell/v1.0"});
    arguments={"-NoProfile","-NonInteractive","-ExecutionPolicy","Bypass","-File",QDir::toNativeSeparators(script),
        "-Target",QDir::toNativeSeparators(installRoot()),"-Work",QDir::toNativeSeparators(m_work),
        "-ParentId",QString::number(QCoreApplication::applicationPid()),"-Token",m_token};
#else
    program="/bin/sh";arguments={script,installRoot(),staged,m_work,QString::number(QCoreApplication::applicationPid()),m_token};
#endif
    QProcess helper;helper.setProgram(program);helper.setArguments(arguments);helper.setWorkingDirectory(m_work);
    helper.setStandardOutputFile(updateRoot()+"/last-update.log",QIODevice::Truncate);helper.setStandardErrorFile(updateRoot()+"/last-update.log",QIODevice::Append);
    if(program.isEmpty() || !helper.startDetached()){fail();return;}
    setState("installing");
    auto timer=new QTimer(this);timer->setInterval(100);
    connect(timer,&QTimer::timeout,this,[this,timer,attempt=0]() mutable {
        if(QFileInfo::exists(m_work+"/helper-ready")){
            timer->stop();timer->deleteLater();
            if(canRestart && !canRestart()){
                writeObject(m_work+"/cancel.json",{{"cancelled",true}});setState("busy");return;
            }
            emit restartRequested();return;
        }
        if(++attempt>=900){timer->stop();timer->deleteLater();writeObject(m_work+"/cancel.json",{{"cancelled",true}});setState("error");}
    });timer->start();
#endif
}
bool AppUpdate::acknowledgeLaunch(const QString &token) {
    if(!QRegularExpression("^[0-9a-f]{32}$").match(token).hasMatch())return false;
    AppUpdate update;const auto work=updateRoot()+"/"+token;const auto pending=readObject(work+"/pending.json");
    if(update.appleStore() || pending["token"]!=token || QDir::cleanPath(pending["target"].toString())!=update.installRoot())return false;
    return writeObject(work+"/started.json",{{"token",token},{"pid",QCoreApplication::applicationPid()}});
}
