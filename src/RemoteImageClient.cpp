#include "RemoteImageClient.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QLocalSocket>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
#include <cstdio>

namespace {
QByteArray contents(QString path){QFile f(path);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray();}
QJsonObject json(QString path){return QJsonDocument::fromJson(contents(path)).object();}
bool save(QString path,QByteArray bytes){
    QSaveFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(bytes)==bytes.size()&&f.commit();
}
bool save(QString path,QJsonObject data){return save(path,QJsonDocument(data).toJson());}
QString quote(QString text){return "'"+text.replace("'","'\\''")+"'";}
QString hash(QByteArray bytes){return QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex();}
}

RemoteImageClient::RemoteImageClient(QObject *parent):QObject(parent){
    const auto fallback=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/config/gpu-server.json";
    m_configPath=qEnvironmentVariable("MOSS_GPU_CONFIG",fallback);
    m_base=QDir(QFileInfo(m_configPath).absolutePath()+"/..").absolutePath();
    m_config=json(m_configPath);
    if(m_config.isEmpty()){
        m_config=json(":/remote/config/gpu-server.json");
        QDir().mkpath(QFileInfo(m_configPath).absolutePath());
        saveConfig();
    }
    m_process.setProcessChannelMode(QProcess::SeparateChannels);
    connect(&m_process,&QProcess::started,this,[this]{m_process.write(m_payload);m_process.closeWriteChannel();});
    connect(&m_process,&QProcess::readyReadStandardOutput,this,[this]{m_stdout+=m_process.readAllStandardOutput();});
    connect(&m_process,&QProcess::readyReadStandardError,this,[this]{m_stderr+=m_process.readAllStandardError();});
    connect(&m_process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,&RemoteImageClient::response);
    connect(&m_process,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){
        if(error==QProcess::FailedToStart){m_timeout.stop();clearPassword();transportFailure("无法启动系统 SSH："+m_process.errorString());}
    });
    m_poll.setSingleShot(true);
    connect(&m_poll,&QTimer::timeout,this,&RemoteImageClient::poll);
    m_timeout.setSingleShot(true);
    connect(&m_timeout,&QTimer::timeout,this,[this]{m_stderr+="\nSSH operation timed out";m_process.kill();});
    m_auth.setSocketOptions(QLocalServer::UserAccessOption);
    connect(&m_auth,&QLocalServer::newConnection,this,[this]{
        while(auto socket=m_auth.nextPendingConnection()){
            connect(socket,&QLocalSocket::disconnected,socket,&QObject::deleteLater);
            // The private local socket carries the password only to ssh-askpass.
            if(++m_authRequests<=3)socket->write(m_password);
            socket->disconnectFromServer();
        }
    });
}

RemoteImageClient::~RemoteImageClient(){
    m_poll.stop();m_timeout.stop();m_process.disconnect(this);
    if(busy()){m_process.kill();m_process.waitForFinished(1500);}
    clearPassword(); // The detached Ubuntu job and pending record remain recoverable.
}

int RemoteImageClient::askpass(){
    QLocalSocket socket;
    socket.connectToServer(qEnvironmentVariable("MOSS_SSH_ASKPASS_SOCKET"));
    if(!socket.waitForConnected(3000))return 1;
    QByteArray secret;
    while(socket.state()!=QLocalSocket::UnconnectedState){
        if(socket.bytesAvailable()||socket.waitForReadyRead(3000))secret+=socket.readAll();
        else break;
    }
    secret+=socket.readAll();
    if(secret.isEmpty())return 1;
    const bool ok=std::fwrite(secret.constData(),1,size_t(secret.size()),stdout)==size_t(secret.size());
    std::fputc('\n',stdout);std::fflush(stdout);secret.fill('\0');
    return ok?0:1;
}

QString RemoteImageClient::archiveRoot()const{
    const auto path=m_config["appRuns"].toString("runs/app-gpu");
    return QDir::cleanPath(QDir::isAbsolutePath(path)?path:m_base+"/"+path);
}
QStringList RemoteImageClient::archiveRoots()const{
    QStringList roots={archiveRoot()};
    for(const auto &value:m_config["previousAppRuns"].toArray()){
        const auto path=value.toString();
        if(QDir::isAbsolutePath(path))roots.append(QDir::cleanPath(path));
    }
    roots.removeDuplicates();return roots;
}
QString RemoteImageClient::knownHosts()const{
    const auto path=m_config["knownHosts"].toString("config/gpu-known-hosts");
    const auto resolved=QDir::isAbsolutePath(path)?path:m_base+"/"+path;
    if(QFileInfo::exists(resolved))return resolved;
    const auto fallback=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/ssh/gpu-known-hosts";
    QDir().mkpath(QFileInfo(fallback).absolutePath());
    if(!QFileInfo::exists(fallback))save(fallback,contents(":/remote/config/gpu-known-hosts"));
    return fallback;
}
bool RemoteImageClient::saveConfig(){
    if(save(m_configPath,m_config))return true;
    m_error="无法保存服务器配置："+m_configPath;emit changed();return false;
}
bool RemoteImageClient::configure(QString host,QString user,int port){
    if(!available()||active()||busy())return false;
    if(!QRegularExpression("^[A-Za-z0-9][A-Za-z0-9.-]*$").match(host).hasMatch()||
       !QRegularExpression("^[A-Za-z_][A-Za-z0-9_-]*$").match(user).hasMatch()||port<1||port>65535){
        m_error="请填写有效的主机地址、用户名和端口";emit changed();return false;
    }
    if(host!=this->host()||user!=this->user()||port!=this->port()){
        m_ready=false;m_config["host"]=host;m_config["user"]=user;m_config["port"]=port;
    }
    m_error.clear();const bool ok=saveConfig();emit changed();return ok;
}
void RemoteImageClient::chooseArchive(QUrl directory){
    if(active()||busy()||directory.toLocalFile().isEmpty())return;
    const auto previous=m_config;auto roots=archiveRoots();
    m_config["appRuns"]=QDir(directory.toLocalFile()).absolutePath();
    roots.removeAll(archiveRoot());m_config["previousAppRuns"]=QJsonArray::fromStringList(roots);
    if(!saveConfig())m_config=previous;
    emit archivesChanged();emit changed();
}
void RemoteImageClient::openArchive(){QDir().mkpath(archiveRoot());QDesktopServices::openUrl(QUrl::fromLocalFile(archiveRoot()));}
QString RemoteImageClient::latestPending()const{
    QString latest;
    for(const auto &root:archiveRoots()){
        QDir dir(root);
        for(const auto &name:dir.entryList(QDir::Dirs|QDir::NoDotAndDotDot))
            if(QFileInfo::exists(dir.filePath(name+"/remote-pending.json"))&&
               (latest.isEmpty()||name>QFileInfo(latest).fileName()))latest=dir.filePath(name);
    }
    return latest;
}
int RemoteImageClient::pendingCount()const{
    int count=0;for(const auto &root:archiveRoots()){QDir dir(root);
    for(const auto &name:dir.entryList(QDir::Dirs|QDir::NoDotAndDotDot))
        if(QFileInfo::exists(dir.filePath(name+"/remote-pending.json")))++count;
    }
    return count;
}
QJsonObject RemoteImageClient::activeConfig()const{return active()?m_job["config"].toObject():m_config;}
QStringList RemoteImageClient::sshArguments(const QJsonObject &config)const{
    const auto identity=config["user"].toString()+"@"+config["host"].toString()+":"+QString::number(config["port"].toInt(22));
    const auto cache=QStandardPaths::writableLocation(QStandardPaths::CacheLocation)+"/ssh";
    QDir().mkpath(cache);QFile::setPermissions(cache,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner);
    const auto pin=knownHosts();
    const auto socket=cache+"/"+hash(identity.toUtf8()+contents(pin)).left(12)+".sock";
    return {"-T","-p",QString::number(config["port"].toInt(22)),
        "-o","ConnectTimeout=8","-o","ServerAliveInterval=10","-o","ServerAliveCountMax=2",
        "-o","ControlMaster=auto","-o","ControlPersist=600","-S",socket,
        "-o","StrictHostKeyChecking=yes","-o","HostKeyAlias="+config["hostKeyAlias"].toString("moss-image-gpu"),
        "-o","UserKnownHostsFile="+pin,"-o",QString("BatchMode=")+(m_password.isEmpty()?"yes":"no"),
        "-o","NumberOfPasswordPrompts=1",config["user"].toString()+"@"+config["host"].toString(),
        "python3 -c "+quote(QString::fromUtf8(contents(":/remote/scripts/remote_image_worker.py")))};
}
void RemoteImageClient::clearPassword(){
    m_password.fill('\0');m_password.clear();m_auth.close();m_authRequests=0;
}
void RemoteImageClient::connectServer(QString password){
    if(!available()){m_error="公开版仅支持本地运行";emit changed();return;}
    if(busy())return;
    m_poll.stop();m_error.clear();m_ready=false;
    m_password=password.toUtf8();password.fill(QChar('\0'));
    if(!m_password.isEmpty()&&!m_auth.listen("moss-auth-"+QUuid::createUuid().toString(QUuid::Id128))){
        m_error="无法准备安全的 SSH 认证通道";clearPassword();emit changed();return;
    }
    m_status="正在连接并检查 GPU…";request("probe");
}
void RemoteImageClient::request(QString action,QJsonObject payload){
    if(!available()||busy())return;
    m_action=action;m_stdout.clear();m_stderr.clear();
    payload["action"]=action;payload["config"]=activeConfig();
    if(active())payload["jobId"]=m_job["jobId"];
    m_payload=QJsonDocument(payload).toJson(QJsonDocument::Compact);
    auto env=QProcessEnvironment::systemEnvironment();
    if(!m_password.isEmpty()){
        env.insert("SSH_ASKPASS",QCoreApplication::applicationFilePath());
        env.insert("SSH_ASKPASS_REQUIRE","force");env.insert("DISPLAY",":0");
        env.insert("MOSS_SSH_ASKPASS_SOCKET",m_auth.fullServerName());
    }
    m_process.setProcessEnvironment(env);
    m_process.start("/usr/bin/ssh",sshArguments(activeConfig()));
    m_timeout.start(action=="fetch"?120000:45000);emit changed();
}
bool RemoteImageClient::start(QString directory,QString kind,QString quant,const QStringList &args){
    if(active()||busy()||!modelReady(quant))return false;
    auto value=[&](QString flag){int i=args.indexOf(flag);return i>=0&&i+1<args.size()?args[i+1]:QString();};
    m_job={{"jobId",QFileInfo(directory).fileName()},{"kind",kind},{"quant",quant},{"config",m_config},
        {"width",value("-W").toInt()},{"height",value("-H").toInt()},{"steps",value("--steps").toInt()},
        {"seed",value("--seed").toInt()},{"cfg",value("--cfg-scale").toDouble()},
        {"flowShift",value("--flow-shift").toDouble()},{"negativePrompt",value("--negative-prompt")},
        {"prompt",QString::fromUtf8(contents(directory+"/prompt.txt"))}};
    if(kind=="edit")m_job["strength"]=value("--strength").toDouble();
    if(!save(directory+"/remote-pending.json",m_job)){m_error="无法保存远程任务恢复记录";emit changed();return false;}
    m_directory=directory;m_cancel=false;m_offset=0;m_failures=0;m_error.clear();
    auto payload=m_job;
    payload["workerSource"]=QString::fromUtf8(contents(":/remote/scripts/remote_image_worker.py"));
    if(kind=="edit")for(const auto &name:{"init.png","mask.png"})
        payload[name]=QString::fromLatin1(contents(directory+"/"+name).toBase64());
    m_status="正在提交远程任务…";emit phase(m_status);request("submit",payload);return true;
}
bool RemoteImageClient::resume(QString directory){
    if(!available()||active()||busy())return false;
    auto pending=json(directory+"/remote-pending.json");
    if(pending.isEmpty())return false;
    // Reconnect to the configured address after an IP change, retaining the saved model paths and identity.
    auto config=pending["config"].toObject();
    if(config["hostKeyAlias"]==m_config["hostKeyAlias"]&&config["user"]==m_config["user"]){
        config["host"]=m_config["host"];config["port"]=m_config["port"];pending["config"]=config;
    }
    m_job=pending;m_directory=directory;m_cancel=false;m_offset=0;m_failures=0;m_error.clear();poll();return true;
}
void RemoteImageClient::cancel(){
    if(!active())return;
    m_cancel=true;m_poll.stop();m_status="正在取消远程任务并回收日志…";emit phase(m_status);
    if(!busy())request("cancel");
}
void RemoteImageClient::poll(){
    if(active()&&!busy())request(m_cancel?"cancel":"poll",{{"offset",double(m_offset)}});
}
void RemoteImageClient::transportFailure(QString error){
    m_ready=false;m_error=error;
    if(!active()){m_status="连接失败";emit changed();return;}
    ++m_failures;
    if(m_failures>=3||m_cancel){
        m_status="连接中断，任务已保留；连接后可恢复并回收";
        finish(1,false,true);return;
    }
    m_status="连接暂时中断，正在重试并保留远程任务…";emit phase(m_status);emit changed();
    m_poll.start(4000);
}
void RemoteImageClient::finish(int code,bool cancelled,bool recoverable){
    m_poll.stop();
    if(!recoverable)QFile::remove(m_directory+"/remote-pending.json");
    m_directory.clear();m_job={};m_cancel=false;
    emit changed();emit completed(code,cancelled,recoverable);
}
void RemoteImageClient::response(int code,QProcess::ExitStatus exitStatus){
    m_timeout.stop();m_stdout+=m_process.readAllStandardOutput();m_stderr+=m_process.readAllStandardError();clearPassword();
    if(code!=0||exitStatus!=QProcess::NormalExit){
        transportFailure("SSH 连接失败："+QString::fromUtf8(m_stderr).right(1800));return;
    }
    QJsonParseError parse;auto reply=QJsonDocument::fromJson(m_stdout,&parse).object();m_stdout.clear();m_payload.clear();
    if(parse.error!=QJsonParseError::NoError||reply.isEmpty()){transportFailure("服务器返回了无法读取的响应");return;}
    if(!reply["ok"].toBool()){
        m_error=reply["error"].toString("远程操作失败");m_status=m_error;
        if(active()){
            // A positive protocol rejection is distinct from an uncertain SSH disconnect.
            const bool recoverable=m_action!="submit"&&!reply["notFound"].toBool();
            finish(1,false,recoverable);
        }else emit changed();
        return;
    }
    m_failures=0;
    if(m_action=="probe"){
        m_ready=true;m_modelsReady=reply["modelsReady"].toObject();m_status=reply["gpu"].toString();m_error.clear();
        emit changed();if(active())m_poll.start(0);return;
    }
    if(m_action=="submit"){m_status="远程 GPU 正在准备模型…";emit phase(m_status);m_poll.start(500);}
    else if(m_action=="cancel"){m_cancel=false;m_poll.start(500);}
    else if(m_action=="poll"){
        m_offset=qint64(reply["offset"].toDouble());
        const auto bytes=QByteArray::fromBase64(reply["logBase64"].toString().toLatin1());
        if(!bytes.isEmpty())emit output(bytes);
        const auto state=reply["state"].toObject();
        const auto status=state["status"].toString();
        if(status=="success"||status=="failed"||status=="cancelled"){
            m_status="正在回收图片和日志…";emit phase(m_status);request("fetch");return;
        }
        if(state["stage"]=="validating"){m_status="正在校验远程模型…";emit phase(m_status);}
        if(m_cancel){request("cancel");return;}
        m_poll.start(1500);
    }else if(m_action=="fetch"){
        const auto state=reply["state"].toObject();
        const auto files=reply["files"].toObject();
        const QStringList allowed={"image.png","raw.png","engine.log","remote-request.json","remote-state.json","remote-worker.log","remote-worker.py"};
        QJsonObject hashes;
        for(auto it=files.begin();it!=files.end();++it){
            if(!allowed.contains(it.key())){m_error="回收文件名无效";finish(1,false,true);return;}
            const auto file=it.value().toObject();const auto data=QByteArray::fromBase64(file["base64"].toString().toLatin1());
            if(hash(data)!=file["sha256"].toString()){m_error="回收文件校验失败："+it.key();finish(1,false,true);return;}
            hashes[it.key()]=file["sha256"];
        }
        const auto outputName=m_job["kind"]=="edit"?"raw.png":"image.png";
        if(state["status"]=="success"&&(!files.contains(outputName)||
            files[outputName].toObject()["sha256"]!=state["imageSha256"])){
            m_error="远端输出与生成记录不一致";finish(1,false,true);return;
        }
        for(auto it=files.begin();it!=files.end();++it){
            if(!save(m_directory+"/"+it.key(),QByteArray::fromBase64(it.value().toObject()["base64"].toString().toLatin1()))){
                m_error="无法保存回收文件："+it.key();finish(1,false,true);return;
            }
        }
        if(!save(m_directory+"/remote-transfer.json",QJsonObject{{"host",activeConfig()["host"]},{"jobId",m_job["jobId"]},
            {"retrievedAt",QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},{"sha256",hashes}})){
            m_error="无法保存回收校验记录";finish(1,false,true);return;
        }
        m_status="图片与日志已回收到项目";m_error=state["message"].toString();
        finish(state["status"]=="success"?0:state["exitCode"].toInt(1),state["status"]=="cancelled",false);return;
    }
    emit changed();
}
