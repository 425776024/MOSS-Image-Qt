#include "RuntimeManager.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTimer>
#include <QUuid>
#include <QtConcurrent>
#include <filesystem>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
bool sha(const QString &text) { return QRegularExpression("^[0-9a-f]{64}$").match(text).hasMatch(); }
bool safe(const QString &path) {
    if(!QRegularExpression("^[A-Za-z0-9_./-]+$").match(path).hasMatch())return false;
    for(const auto &part:path.split('/'))if(part.isEmpty()||part=="."||part==".."||part.endsWith('.'))return false;
    return true;
}
bool unlinked(QString path) {
    while(!path.isEmpty()) {
        const QFileInfo info(path);
        if(info.isSymLink()||info.isJunction())return false;
        const auto parent=info.absolutePath();if(parent==path)break;path=parent;
    }
    return true;
}
QString hashFile(const QString &path,const std::shared_ptr<std::atomic_bool> &cancelled) {
    QFile file(path);if(!unlinked(path)||!file.open(QIODevice::ReadOnly))return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while(!file.atEnd()) {
        if(*cancelled)return {};
        const auto bytes=file.read(1024*1024);if(bytes.isEmpty()&&file.error()!=QFile::NoError)return {};
        hash.addData(bytes);
    }
    return QString::fromLatin1(hash.result().toHex());
}
QString verify(const QString &root,const QJsonArray &files,const std::shared_ptr<std::atomic_bool> &cancelled) {
    for(const auto &value:files) {
        const auto row=value.toObject();const auto path=root+"/"+row["path"].toString();
        if(*cancelled)return "cancelled";
        if(QFileInfo(path).size()!=row["bytes"].toInteger()||hashFile(path,cancelled)!=row["sha256"].toString())return "invalid";
    }
    return {};
}
bool save(const QString &path,const QByteArray &bytes) {
    QSaveFile file(path);return file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size()&&file.commit();
}
QString uuid() { return QUuid::createUuid().toString(QUuid::Id128); }
}

RuntimeManager::RuntimeManager(QObject *parent,QUrl testTransport):QObject(parent),m_testTransport(testTransport) {
    m_extractTimeout.setSingleShot(true);m_extractTimeout.setInterval(600000);
    connect(&m_extractTimeout,&QTimer::timeout,this,[this]{if(m_extract.state()!=QProcess::NotRunning)m_extract.kill();});
    connect(&m_download,&ModelHttpDownload::progress,this,[this](qint64 bytes){
        emit progress(m_total?double(m_done+bytes)/m_total:0);
    });
    connect(&m_download,&ModelHttpDownload::completed,this,[this](QString error,bool cancelled){
        if(cancelled||*m_cancelled){finish();return;}
        if(!error.isEmpty()){qWarning()<<"Runtime download failed:"<<error;finish("下载失败，请重试");return;}
        const auto path=partPath();const auto expected=m_component["sha256"].toString();const auto stop=m_cancelled;
        background([=]{return hashFile(path,stop)==expected?QString():QString("checksum");},[this](QString error){
            if(*m_cancelled){finish();return;}
            if(!error.isEmpty()){QFile::remove(partPath());QFile::remove(ModelHttpDownload::journalPath(partPath()));finish("资源校验失败，请重试");return;}
            extract();
        });
    });
    connect(&m_extract,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus status){
        m_extractTimeout.stop();
        if(!m_busy)return;
        if(*m_cancelled){finish();return;}
        if(code||status!=QProcess::NormalExit){qWarning()<<"Runtime extraction failed:"<<m_extract.readAllStandardError().left(4096);finish("无法准备加速资源，请重试");return;}
        const auto path=m_staging+"/files";const auto files=m_component["files"].toArray();const auto stop=m_cancelled;
        background([=]{return verify(path,files,stop);},[this](QString error){
            if(*m_cancelled){finish();return;}
            if(!error.isEmpty()){finish("资源校验失败，请重试");return;}
            if(!QDir().rename(m_staging+"/files",componentPath())){finish("无法保存加速资源，请检查磁盘空间");return;}
            QDir(m_staging).removeRecursively();m_staging.clear();
            QFile::remove(partPath());QFile::remove(ModelHttpDownload::journalPath(partPath()));
            m_done+=m_component["bytes"].toInteger();++m_index;next();
        });
    });
    connect(&m_extract,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){
        if(m_busy&&error==QProcess::FailedToStart)finish("无法启动资源准备工具，请重试");
    });
}
RuntimeManager::~RuntimeManager() {
    if(m_cancelled)*m_cancelled=true;
    m_download.disconnect(this);m_download.cancel();m_extract.disconnect(this);
    if(m_extract.state()!=QProcess::NotRunning){m_extract.kill();m_extract.waitForFinished(3000);}
    m_check.disconnect(this);m_check.waitForFinished();
    if(!m_staging.isEmpty())QDir(m_staging).removeRecursively();
}
QString RuntimeManager::defaultRoot() {
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/runtimes";
}
bool RuntimeManager::cudaAvailable() {
#ifdef Q_OS_WIN
    const auto driver=LoadLibraryExW(L"nvcuda.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!driver)return false;
    const auto init=reinterpret_cast<int(WINAPI *)(unsigned)>(GetProcAddress(driver,"cuInit"));
    const auto count=reinterpret_cast<int(WINAPI *)(int*)>(GetProcAddress(driver,"cuDeviceGetCount"));
    int devices=0;const bool available=init&&count&&init(0)==0&&count(&devices)==0&&devices>0;
    FreeLibrary(driver);return available;
#else
    return false;
#endif
}
void RuntimeManager::prepare(QByteArray data,QString root) {
    if(m_busy)return;
    m_busy=true;m_cancelled=std::make_shared<std::atomic_bool>(false);m_index=0;m_done=0;m_total=0;
    m_manifest=QJsonDocument::fromJson(data).object();m_root=QDir(root).absolutePath();
    const auto components=m_manifest["components"].toArray();QSet<QString> names,ids;QJsonArray identities;
    bool valid=m_manifest["schema"].toInt()==1&&m_manifest["platform"]=="windows-x64"&&!components.isEmpty();
    for(const auto &value:components) {
        const auto component=value.toObject();const auto id=component["id"].toString();
        const auto name=component["archive"].toString();const auto url=component["url"].toString();
        valid&=sha(id)&&component["sha256"]==id&&!ids.contains(id)&&safe(name)&&!name.contains('/')&&name.endsWith(id+".zip")
            &&url=="https://github.com/425776024/MOSS-Image-Qt/releases/download/gpu-runtime-components-v1/"+name
            &&component["bytes"].toInteger()>0&&component["bytes"].toInteger()<2LL*1024*1024*1024;
        ids.insert(id);identities.append(id);m_total+=component["bytes"].toInteger();
        const auto files=component["files"].toArray();valid&=!files.isEmpty();
        for(const auto &entry:files) {
            const auto file=entry.toObject();const auto path=file["path"].toString();
            valid&=safe(path)&&!names.contains(path.toLower())&&sha(file["sha256"].toString())&&file["bytes"].toInteger()>0
                &&(path.startsWith("licenses/")||(!path.contains('/')&&path.endsWith(".dll")));
            names.insert(path.toLower());
        }
    }
    const auto identity=QCryptographicHash::hash(QJsonDocument(identities).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex();
    valid&=m_manifest["id"].toString()==QString::fromLatin1(identity);
    for(const auto &required:{"onnxruntime.dll","onnxruntime_providers_shared.dll","onnxruntime_providers_cuda.dll","cudart64_12.dll","cublas64_12.dll","cublasLt64_12.dll","cudnn64_9.dll","cufft64_11.dll","curand64_10.dll"})valid&=names.contains(QString::fromLatin1(required).toLower());
    if(!valid){finish("加速资源清单无效，请重新安装应用");return;}
    if(!unlinked(m_root)||!QDir().mkpath(m_root)){finish("无法访问加速资源目录");return;}
    if(!unlinked(m_root+"/.prepare.lock")){finish("加速资源目录无效");return;}
    m_lock=std::make_unique<QLockFile>(m_root+"/.prepare.lock");
    m_lock->setStaleLockTime(0);
    if(!m_lock->tryLock(0)){finish("另一窗口正在准备加速资源，请稍后重试");return;}
    for(const auto &folder:{"components","downloads","sets","staging"}) {
        const auto path=m_root+"/"+folder;
        if(!unlinked(path)||!QDir().mkpath(path)){finish("无法访问加速资源目录");return;}
    }
    next();
}
QString RuntimeManager::componentPath() const {return m_root+"/components/"+m_component["id"].toString();}
QString RuntimeManager::partPath() const {return m_root+"/downloads/"+m_component["id"].toString()+".part";}
void RuntimeManager::background(std::function<QString()> work,std::function<void(QString)> done) {
    m_check.disconnect(this);
    connect(&m_check,&QFutureWatcher<QString>::finished,this,[this,done]{done(m_check.result());});
    m_check.setFuture(QtConcurrent::run(std::move(work)));
}
void RuntimeManager::next() {
    if(*m_cancelled){finish();return;}
    const auto components=m_manifest["components"].toArray();
    emit progress(m_total?double(m_done)/m_total:0);
    if(m_index==components.size()){assemble();return;}
    m_component=components[m_index].toObject();
    const auto path=componentPath();const auto files=m_component["files"].toArray();const auto stop=m_cancelled;
    if(!unlinked(path)){finish("加速资源目录无效");return;}
    background([=]{return verify(path,files,stop);},[this](QString error){
        if(*m_cancelled){finish();return;}
        if(error.isEmpty()){m_done+=m_component["bytes"].toInteger();++m_index;next();return;}
        if(QFileInfo::exists(componentPath())&&!QDir().rename(componentPath(),componentPath()+".invalid-"+uuid())){finish("加速资源被占用，请关闭其它窗口后重试");return;}
        download();
    });
}
void RuntimeManager::download() {
    if(!unlinked(partPath())||!unlinked(ModelHttpDownload::journalPath(partPath()))){finish("下载缓存无效");return;}
    qint64 unpacked=0;for(const auto &f:m_component["files"].toArray())unpacked+=f.toObject()["bytes"].toInteger();
    if(QStorageInfo(m_root).bytesAvailable()<unpacked*2+m_component["bytes"].toInteger()+64*1024*1024){finish("空间不足，请清理后重试");return;}
    const QUrl url=m_testTransport.isEmpty()?QUrl(m_component["url"].toString()):m_testTransport.resolved(QUrl(m_component["archive"].toString()));
    m_download.start(partPath(),m_component["bytes"].toInteger(),m_component["sha256"].toString(),{url},"windows-runtime",false);
}
void RuntimeManager::extract() {
    m_staging=m_root+"/staging/"+uuid();
    if(!QDir().mkpath(m_staging+"/files")||!save(m_staging+"/component.json",QJsonDocument(m_component).toJson())){finish("无法准备加速资源，请检查磁盘空间");return;}
    QFile script(":/runtime/scripts/install_runtime.ps1");
    if(!script.open(QIODevice::ReadOnly)||!save(m_staging+"/install.ps1",script.readAll())){finish("资源安装工具缺失，请重新安装应用");return;}
    m_extractTimeout.start();
#ifdef Q_OS_WIN
    wchar_t system[MAX_PATH];const auto length=GetSystemDirectoryW(system,MAX_PATH);
    if(!length||length>=MAX_PATH){finish("无法定位系统资源安装工具");return;}
    const auto powershell=QString::fromWCharArray(system,int(length))+"/WindowsPowerShell/v1.0/powershell.exe";
    m_extract.start(powershell,{"-NoProfile","-NonInteractive","-ExecutionPolicy","Bypass","-File",m_staging+"/install.ps1",
        "-Archive",partPath(),"-Manifest",m_staging+"/component.json","-Destination",m_staging+"/files"});
#else
    // The production downloader is Windows-only. A portable PowerShell enables
    // exercising the identical extraction script in development tests on macOS.
    const auto powershell=QStandardPaths::findExecutable("pwsh");
    if(powershell.isEmpty()){finish("Windows runtime extraction requires PowerShell");return;}
    m_extract.start(powershell,{"-NoProfile","-NonInteractive","-File",m_staging+"/install.ps1",
        "-Archive",partPath(),"-Manifest",m_staging+"/component.json","-Destination",m_staging+"/files"});
#endif
}
void RuntimeManager::assemble() {
    const auto target=m_root+"/sets/"+m_manifest["id"].toString();
    QJsonArray files;for(const auto &c:m_manifest["components"].toArray())for(const auto &f:c.toObject()["files"].toArray())files.append(f);
    const auto stop=m_cancelled;const auto components=m_manifest["components"].toArray();const auto root=m_root;
    m_staging=m_root+"/staging/"+uuid();const auto stage=m_staging;
    background([=]() -> QString {
        if(!unlinked(target))return "加速资源目录无效";
        if(verify(target,files,stop).isEmpty())return {};
        if(*stop)return {};
        if(!QDir().mkpath(stage))return "无法准备加速资源";
        for(const auto &value:components) {
            const auto component=value.toObject();
            for(const auto &entry:component["files"].toArray()) {
                if(*stop)return {};
                const auto row=entry.toObject();const auto path=row["path"].toString();
                const auto source=root+"/components/"+component["id"].toString()+"/"+path;
                const auto destination=stage+"/"+path;
                if(!QDir().mkpath(QFileInfo(destination).absolutePath()))return "无法准备加速资源";
                std::error_code error;
                std::filesystem::create_hard_link(std::filesystem::path(source.toStdU16String()),std::filesystem::path(destination.toStdU16String()),error);
                if(error&&!QFile::copy(source,destination))return "无法保存加速资源，请检查磁盘空间";
            }
        }
        if(QFileInfo::exists(target)&&!QDir().rename(target,target+".invalid-"+uuid()))return "加速资源被占用，请稍后重试";
        if(!QDir().rename(stage,target))return "无法保存加速资源";
        return {};
    },[this,target](QString error){finish(error,error.isEmpty()?target:QString());});
}
void RuntimeManager::cancel() {
    if(!m_busy)return;
    *m_cancelled=true;
    if(m_extract.state()!=QProcess::NotRunning)m_extract.kill();
    else if(m_check.isRunning())return;
    else m_download.cancel();
}
void RuntimeManager::finish(QString error,QString directory) {
    if(!m_busy)return;
    m_extractTimeout.stop();
    const bool cancelled=*m_cancelled;
    if(!m_staging.isEmpty()){QDir(m_staging).removeRecursively();m_staging.clear();}
    m_lock.reset();m_busy=false;
    emit completed(cancelled?QString():directory,cancelled?QString():error,cancelled);
}
