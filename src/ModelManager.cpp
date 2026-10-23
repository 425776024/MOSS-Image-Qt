#ifdef MOSS_APP_STORE
#include "AppleAssetDownload.h"
#endif
#include "ModelManager.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTimer>
#include <QtConcurrent>
#include <cerrno>
#include <cstring>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
QByteArray read(QString path) { QFile f(path);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray(); }
QList<ModelAsset> parse(const QJsonObject &catalog) {
    QList<ModelAsset> result;
    for(const auto &v:catalog["files"].toArray()) {
        const auto f=v.toObject();
        result.append({f["group"].toString(),f["path"].toString(),f["repo"].toString(),f["revision"].toString(),
            f["file"].toString(),f["sha256"].toString(),f["resource"].toString(),f["bytes"].toInteger()});
    }
    return result;
}
QString hashFile(QString path) {
    QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    return hash.addData(&f)?QString::fromLatin1(hash.result().toHex()):QString();
}
QString sizeText(qint64 bytes) { return QLocale().formattedDataSize(bytes,2,QLocale::DataSizeSIFormat); }
QString installModelFile(const QString &source,const QString &destination) {
#ifdef Q_OS_WIN
    // The Windows CRT rename cannot replace an existing file. Use UTF-16 paths
    // so model repair also works under non-ASCII user and model directories.
    const auto from=QDir::toNativeSeparators(source).toStdWString();
    const auto to=QDir::toNativeSeparators(destination).toStdWString();
    if(MoveFileExW(from.c_str(),to.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))return {};
    const auto code=GetLastError();return QString("Windows error %1").arg(code);
#else
    if(::rename(QFile::encodeName(source).constData(),QFile::encodeName(destination).constData())==0)return {};
    return QString::fromLocal8Bit(std::strerror(errno));
#endif
}
}
QList<ModelAsset> ModelManager::assets() {
    static const auto files=parse(QJsonDocument::fromJson(read(":/models/config/models.json")).object());return files;
}
ModelManager::ModelManager(QObject *parent,QByteArray catalog,QUrl testTransport):QObject(parent),m_testTransport(testTransport) {
    const auto data=QJsonDocument::fromJson(catalog.isEmpty()?read(":/models/config/models.json"):catalog).object();
    m_assets=parse(data);m_groups=data["groups"].toArray().toVariantList();
    QSettings settings;
#ifdef MOSS_APP_STORE
    m_root=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/models";
#else
    m_root=qEnvironmentVariable("MOSS_MODEL_ROOT",settings.value("publicModelRoot").toString());
#endif
    if(m_root.isEmpty())m_root=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/models";
    m_root=QDir(m_root).absolutePath();
#ifdef MOSS_APP_STORE
    m_endpoint=settings.value("storeModelEndpoint","apple-assets").toString();
#else
    m_endpoint=settings.value("modelEndpoint","https://hf-mirror.com").toString();
#endif
    if(!sourceValues().contains(m_endpoint))m_endpoint=sourceValues().first();
    m_quant=qEnvironmentVariable("MOSS_IMAGE_QUANT",settings.value("imageQuant","Q8_0").toString());
    if(m_quant!="Q4_K"&&m_quant!="Q8_0")m_quant="Q8_0";
    if(!settings.contains("imageQuant")&&!qEnvironmentVariableIsSet("MOSS_IMAGE_QUANT")&&ready("image-q4")&&!ready("image-q8"))m_quant="Q4_K";
}
ModelManager::~ModelManager() {
    if(m_reply){m_reply->disconnect(this);m_reply->abort();}
    m_part.close();
}
void ModelManager::setRoot(QString value) {
    if(!rootSelectable())return;
    if(locked()||value.isEmpty()||!QDir::isAbsolutePath(value))return;
    m_root=QDir::cleanPath(value);QSettings().setValue("publicModelRoot",m_root);m_error.clear();emit changed();
}
QStringList ModelManager::sourceNames() const {
#ifdef MOSS_APP_STORE
    return {"Apple · App Store 资源包", "国内镜像 · hf-mirror.com", "Hugging Face · huggingface.co"};
#else
    return {"国内镜像 · hf-mirror.com", "Hugging Face · huggingface.co"};
#endif
}
QStringList ModelManager::sourceValues() const {
#ifdef MOSS_APP_STORE
    return {"apple-assets", "https://hf-mirror.com", "https://huggingface.co"};
#else
    return {"https://hf-mirror.com", "https://huggingface.co"};
#endif
}
void ModelManager::setEndpoint(QString value) {
    if(locked()||!sourceValues().contains(value))return;
    m_endpoint=value;
#ifdef MOSS_APP_STORE
    QSettings().setValue("storeModelEndpoint",value);
#else
    QSettings().setValue("modelEndpoint",value);
#endif
    emit changed();
}
void ModelManager::setImageQuant(QString value) {
    if(locked()||(value!="Q4_K"&&value!="Q8_0"))return;
    m_quant=value;QSettings().setValue("imageQuant",value);emit changed();
}
QStringList ModelManager::expand(QStringList capabilities) const {
    QStringList result;
    for(const auto &id:capabilities) {
        if(id=="image")result<<"image-shared"<<(m_quant=="Q4_K"?"image-q4":"image-q8");
        else for(const auto &row:m_groups)if(row.toMap()["id"]==id){result<<id;break;}
    }
    result.removeDuplicates();return result;
}
QList<ModelAsset> ModelManager::selected(QStringList ids) const {
    QList<ModelAsset> files;for(const auto &a:m_assets)if(ids.contains(a.group))files<<a;return files;
}
bool ModelManager::safePath(QString relative) const {
    if(relative.isEmpty()||QDir::isAbsolutePath(relative)||QDir::cleanPath(relative)!=relative||relative.split('/').contains(".."))return false;
    // Check every ancestor including the root: never follow a linked model root,
    // group directory, model, or partial download during mutation or readiness.
    QString path=QDir(m_root).absoluteFilePath(relative);
    while(!path.isEmpty()&&path!="/") {
        if(QFileInfo(path).isSymLink())return false;
        if(path==QDir(m_root).absolutePath())break;
        const auto parent=QFileInfo(path).absolutePath();if(parent==path)break;path=parent;
    }
    return true;
}
bool ModelManager::present(const ModelAsset &a) const {
    const QFileInfo info(m_root+"/"+a.path);
    return safePath(a.path)&&info.isFile()&&info.size()==a.bytes;
}
bool ModelManager::ready(QString capability) const {
    const auto files=selected(expand({capability}));if(files.isEmpty())return false;
    for(const auto &a:files)if(!present(a))return false;return true;
}
QVariantList ModelManager::groups() const {
    QVariantList result;
    for(const auto &value:m_groups) {
        auto row=value.toMap();const auto id=row["id"].toString();qint64 total=0,installed=0;QStringList sources;
        for(const auto &a:selected({id})) {
            total+=a.bytes;if(safePath(a.path)&&QFileInfo(m_root+"/"+a.path).isFile())installed+=QFileInfo(m_root+"/"+a.path).size();
            if(!a.repo.isEmpty())sources<<a.repo;
        }
        sources.removeDuplicates();row["ready"]=ready(id);row["installedBytes"]=installed;row["totalBytes"]=total;
        row["sizeText"]=sizeText(installed)+" / "+sizeText(total);row["sources"]=sources.join("、");
        row["sourceUrl"]=sources.isEmpty()?QString():"https://huggingface.co/"+sources.first();result<<row;
    }
    return result;
}
double ModelManager::partialBytes() const {
    qint64 bytes=0;for(const auto &a:m_assets)if(safePath(a.path+".part")&&QFileInfo(m_root+"/"+a.path+".part").isFile())bytes+=QFileInfo(m_root+"/"+a.path+".part").size();return bytes;
}
bool ModelManager::sfxTermsAccepted() const {
    return QSettings().value("sfxModelTermsVersion").toString()=="2026-10-22";
}
bool ModelManager::sfxTermsRequired() const {
    return expand(m_requested).contains("sfx")&&!sfxTermsAccepted();
}
void ModelManager::acceptSfxTerms() {
    QSettings settings;settings.setValue("sfxModelTermsVersion","2026-10-22");settings.sync();emit changed();
}
QString ModelManager::modelTerms(bool english) const {
    const auto text=QString::fromUtf8(read(":/model-terms/docs/model-terms.md"));
    const auto section=text.indexOf("## English");
    return english&&section>=0?text.mid(section):text.left(section>=0?section:text.size());
}
bool ModelManager::ensure(QStringList capabilities) {
    QStringList missing;for(const auto &id:expand(capabilities))if(!ready(id)||(id=="sfx"&&!sfxTermsAccepted()))missing<<id;
    if(missing.isEmpty())return true;
    // Keep the logical image group so Q4/Q8 can be selected in the dialog.
    m_requested=capabilities;m_error.clear();emit changed();emit downloadRequested();return false;
}
void ModelManager::requestDownload(QString group) {
    if(locked()||expand({group}).isEmpty())return;
    m_requested={group};m_error.clear();emit changed();emit downloadRequested();
}
void ModelManager::dismissRequest(){if(!busy()){m_requested.clear();emit changed();}}
QVariantMap ModelManager::requestInfo() const {
    QStringList names;qint64 bytes=0;
    for(const auto &id:expand(m_requested)) {
        for(const auto &r:m_groups)if(r.toMap()["id"]==id)names<<r.toMap()["title"].toString();
        for(const auto &a:selected({id}))if(!present(a))bytes+=a.bytes;
    }
    return {{"titles",names},{"size",sizeText(bytes)}};
}
QString ModelManager::describeRequest() const {
    const auto info=requestInfo();
    return info["titles"].toStringList().join("、")+"\n待补齐文件约 "+info["size"].toString()+"，已有文件会校验后复用。下载完成后请重新点击生成。";
}
bool ModelManager::acquire() {
    if(locked())return false;
    if(!safePath(".models.lock")||!QDir().mkpath(m_root)){m_error="模型目录不可写或包含符号链接";emit changed();return false;}
    m_lock=std::make_unique<QLockFile>(m_root+"/.models.lock");m_lock->setStaleLockTime(0);
    if(!m_lock->tryLock()){m_lock.reset();m_error="另一个窗口正在管理此模型目录，请稍后重试";emit changed();return false;}
    return true;
}
void ModelManager::startDownload() {
    if(sfxTermsRequired()){m_error="Please accept the sound model terms before downloading or using these models. / 请先同意音效模型使用条款。";emit changed();return;}
    const auto files=selected(expand(m_requested));if(files.isEmpty()||!acquire())return;
    qint64 required=0;for(const auto &a:files)if(!present(a))required+=std::max<qint64>(0,a.bytes-QFileInfo(m_root+"/"+a.path+".part").size());
    if(m_endpoint=="apple-assets"){qint64 largest=0;for(const auto &a:files)largest=std::max(largest,a.bytes);required+=largest;}
    QStorageInfo storage(m_root);
    if(storage.isValid()&&storage.bytesAvailable()<required+64*1024*1024){m_lock.reset();m_error="磁盘空间不足，请先清理模型或更换目录";emit changed();return;}
    m_queue=files;m_busy=true;m_cancelled=false;m_verifyOnly=false;m_error.clear();m_index=0;m_done=0;m_total=0;m_progress=0;
    for(const auto &a:m_queue)m_total+=a.bytes;emit changed();next();
}
void ModelManager::verifyGroup(QString id) {
    const auto files=selected(expand({id}));if(files.isEmpty()||!acquire())return;
    m_queue=files;m_busy=true;m_cancelled=false;m_verifyOnly=true;m_error.clear();m_index=0;m_done=0;m_total=0;m_progress=0;
    for(const auto &a:files)m_total+=a.bytes;emit changed();next();
}
void ModelManager::cancel() {
#ifdef MOSS_APP_STORE
    for(auto transport:findChildren<AppleAssetDownload *>())transport->cancel();
#endif
    if(!m_busy)return;m_cancelled=true;if(m_reply)m_reply->abort();else {m_status="正在取消…";emit changed();} }
void ModelManager::finish(QString error,bool cancelled) {
    m_part.close();m_lock.reset();m_busy=false;m_error=error;
    m_status=cancelled?"已取消，下载残留可续传或清理":error.isEmpty()?(m_verifyOnly?"所选模型 SHA-256 校验通过":"模型已就绪，请重新点击生成"):"操作未完成";
    if(error.isEmpty()&&!cancelled)m_progress=1;
    emit changed();emit completed(error.isEmpty()&&!cancelled);
}
void ModelManager::advance(){m_done+=m_queue[m_index].bytes;++m_index;QTimer::singleShot(0,this,&ModelManager::next);}
void ModelManager::next() {
    if(m_cancelled){finish({},true);return;}
    if(m_index>=m_queue.size()){finish();return;}
    const auto a=m_queue[m_index];const auto path=m_root+"/"+a.path;
    if(!safePath(a.path)||!safePath(a.path+".part")){finish("模型路径包含符号链接，已停止操作");return;}
    if(present(a)){checkFile(a,path,false);return;}
    if(m_verifyOnly){finish("模型缺失或大小不符："+a.path);return;}
    if(!QDir().mkpath(QFileInfo(path).absolutePath())){finish("无法创建模型目录");return;}
    if(!a.resource.isEmpty()) {
        const auto data=read(a.resource);
        if(data.size()!=a.bytes||QCryptographicHash::hash(data,QCryptographicHash::Sha256).toHex()!=a.sha256){finish("应用内模型配置校验失败");return;}
        QSaveFile out(path);
        if(!out.open(QIODevice::WriteOnly)||out.write(data)!=data.size()||!out.commit()){finish("无法写入模型配置");return;}
        advance();return;
    }
    transfer(a);
}
void ModelManager::checkFile(ModelAsset a,QString path,bool install) {
    m_status="校验 "+a.path;emit changed();
    auto watcher=new QFutureWatcher<QString>(this);
    connect(watcher,&QFutureWatcher<QString>::finished,this,[this,watcher,a,path,install]{
        const auto sha=watcher->result();watcher->deleteLater();
        if(m_cancelled){finish({},true);return;}
        if(!safePath(a.path)||!safePath(a.path+".part")){finish("模型路径已改变");return;}
        if(sha!=a.sha256) {
            if(m_verifyOnly){finish("模型 SHA-256 不符："+a.path+"；请下载修复");return;}
            if(install){QFile::remove(path);finish("下载文件 SHA-256 不符，请重试："+a.path);return;}
            // Leave the invalid existing file in place until replacement is verified.
            if(a.resource.isEmpty()){transfer(a);return;}
            QFile::remove(path);QTimer::singleShot(0,this,&ModelManager::next);return;
        }
        if(install) {
            const auto dest=m_root+"/"+a.path;
            const auto error=installModelFile(path,dest);
            if(!error.isEmpty()){finish("无法安装模型文件："+a.path+"；"+error+"。已保留下载文件，请关闭占用文件的程序后重试。");return;}
        }
        advance();
    });
    watcher->setFuture(QtConcurrent::run([path]{return hashFile(path);}));
}
void ModelManager::transfer(ModelAsset a) {
    const auto path=m_root+"/"+a.path+".part";
    if(!safePath(a.path+".part")){finish("下载路径包含符号链接");return;}
    const bool noticeReady=m_endpoint!="apple-assets" ||
        (safePath(".licenses/"+a.sha256+".txt") && QFileInfo(m_root+"/.licenses/"+a.sha256+".txt").isFile());
    // Also reuse complete partials when repairing a same-size corrupt model;
    // that path enters transfer() directly after checking the installed file.
    if(QFileInfo(path).size()==a.bytes && noticeReady){checkFile(a,path,true);return;}
#ifdef MOSS_APP_STORE
    if(m_endpoint=="apple-assets") {
        const auto notice=".licenses/"+a.sha256+".txt";
        if(!safePath(notice)||!QDir().mkpath(m_root+"/.licenses")){finish("无法保存模型许可文件");return;}
        auto transport=new AppleAssetDownload(this);
        m_status="Apple 资源包 · "+a.path;emit changed();
        connect(transport,&AppleAssetDownload::progress,this,[this,a](double value){
            m_progress=double(m_done+qint64(a.bytes*value))/std::max<qint64>(1,m_total);emit changed();
        });
        connect(transport,&AppleAssetDownload::completed,this,[this,transport,a,path](QString error){
            transport->deleteLater();
            if(m_cancelled){finish({},true);return;}
            if(!error.isEmpty()){finish("Apple 资源包未完成："+error+"。可重试或手动选择镜像源。");return;}
            checkFile(a,path,true);
        });
        transport->start("model-"+a.sha256,a.path,path,m_root+"/"+notice,a.bytes);return;
    }
#endif
    m_part.setFileName(path);
    if(!m_part.open(QIODevice::ReadWrite)){finish("无法写入模型文件："+m_part.errorString());return;}
    qint64 offset=m_part.size();if(offset>=a.bytes){if(!m_part.resize(0)){finish("无法重置下载文件");return;}offset=0;}
    m_part.seek(offset);
    QUrl url=m_testTransport.isEmpty()?QUrl(m_endpoint+"/"+a.repo+"/resolve/"+a.revision+"/"+a.file):m_testTransport.resolved(QUrl(a.file));
    QNetworkRequest req(url);req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(60000);req.setRawHeader("Accept-Encoding","identity");
    if(offset)req.setRawHeader("Range","bytes="+QByteArray::number(offset)+"-");
    auto reply=m_network.get(req);m_reply=reply;reply->setReadBufferSize(4*1024*1024);
    m_status=QString("下载 %1 / %2 · %3").arg(m_index+1).arg(m_queue.size()).arg(a.path);emit changed();
    auto consume=[this,reply,offset,a] {
        if(!reply->property("checked").toBool()) {
            const int status=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if(status!=200&&status!=206)return;
            if(status==206&&!reply->rawHeader("Content-Range").startsWith("bytes "+QByteArray::number(offset)+"-")){reply->setProperty("failure","服务器续传位置不正确");reply->abort();return;}
            if(status==200&&offset&&!m_part.resize(0)){reply->setProperty("failure","无法重置下载文件");reply->abort();return;}
            if(status==200&&offset)m_part.seek(0);
            reply->setProperty("checked",true);
        }
        const auto bytes=reply->readAll();
        if(m_part.size()+bytes.size()>a.bytes||m_part.write(bytes)!=bytes.size()){reply->setProperty("failure","磁盘写入失败或下载大小超出清单");reply->abort();return;}
        m_progress=double(m_done+m_part.size())/std::max<qint64>(1,m_total);
        if(!m_refreshTimer.isValid()||m_refreshTimer.elapsed()>100){m_refreshTimer.start();emit changed();}
    };
    connect(reply,&QNetworkReply::readyRead,this,consume);
    connect(reply,&QNetworkReply::finished,this,[this,reply,a,path,consume] {
        if(reply->error()==QNetworkReply::NoError)consume();
        const auto error=reply->error();auto message=reply->property("failure").toString();const auto detail=reply->errorString();
        const auto status=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if(!m_part.flush()&&message.isEmpty())message="无法保存下载文件："+m_part.errorString();
        m_part.close();m_reply=nullptr;reply->deleteLater();
        if(m_cancelled){finish({},true);return;}
        const auto size=QFileInfo(path).size();
        if(error!=QNetworkReply::NoError||!message.isEmpty()||size!=a.bytes){
            if(message.isEmpty())message=error!=QNetworkReply::NoError?detail:QString("文件大小不符：%1 / %2 bytes").arg(size).arg(a.bytes);
            finish(QString("下载未完成，可续传或切换源重试：%1\n文件：%2\n下载源：%3\nHTTP %4；已保存 %5 / %6 bytes")
                .arg(message,a.path,m_endpoint).arg(status).arg(size).arg(a.bytes));return;
        }
        checkFile(a,path,true);
    });
}
QString ModelManager::deletionDescription(QString id) const {
    QString description="删除后可重新下载。只处理此组清单文件，保留作品和未登记的本地文件。";
    if(id=="image-shared")description="删除共用组件后，MOSS Q4 和 Q8 都需要补下载才能运行。";
    if(id=="image-q4"||id=="image-q8")description="仅删除这个量化版本，保留另一版本与共用组件。";
    return description;
}
void ModelManager::removeGroup(QString id) {
    const auto files=selected(expand({id}));if(files.isEmpty()||!acquire())return;
    QStringList failures;
    for(const auto &a:files)for(const auto &rel:{a.path,a.path+".part"}) {
        const auto path=m_root+"/"+rel;
        if(!safePath(rel)){failures<<rel;continue;}
        if(QFileInfo::exists(path)&&(!QFileInfo(path).isFile()||!QFile::remove(path)))failures<<rel;
    }
    m_lock.reset();m_error=failures.isEmpty()?QString():"未删除的路径："+failures.join("、");m_status="模型清理完成";emit changed();
}
void ModelManager::cleanPartials() {
    if(!acquire())return;QStringList failures;
    for(const auto &a:m_assets){const auto rel=a.path+".part",path=m_root+"/"+rel;
        if(!safePath(rel)){failures<<rel;continue;}
        if(QFileInfo::exists(path)&&(!QFileInfo(path).isFile()||!QFile::remove(path)))failures<<rel;
    }
    m_lock.reset();m_error=failures.isEmpty()?QString():"未清理的路径："+failures.join("、");m_status="下载残留清理完成";emit changed();
}
void ModelManager::openRoot(){QDir().mkpath(m_root);QDesktopServices::openUrl(QUrl::fromLocalFile(m_root));}
