#ifdef MOSS_APP_STORE
#include "AppleAssetDownload.h"
#endif
#include "ModelManager.h"
#include "ModelHttpDownload.h"
#include "WarpModelConversion.h"
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
#include <algorithm>

namespace {
QByteArray read(QString path) { QFile f(path);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray(); }
QJsonObject channelCatalog() {
    auto catalog=QJsonDocument::fromJson(read(":/models/config/models.json")).object();
#ifdef MOSS_APP_STORE
    const auto overrides=QJsonDocument::fromJson(read(":/models/config/models-store.json")).object()["files"].toArray();
    if(overrides.isEmpty())qFatal("Missing App Store model catalog; refusing official-model fallback");
    auto files=catalog["files"].toArray();
    for(const auto &replacement:overrides) {
        bool found=false;
        for(int i=0;i<files.size();++i)if(files[i].toObject()["path"]==replacement.toObject()["path"]){files[i]=replacement;found=true;break;}
        if(!found)files.append(replacement);
    }
    catalog["files"]=files;
    auto groups=catalog["groups"].toArray();
    for(int i=0;i<groups.size();++i)if(groups[i].toObject()["id"]=="ditto") {
        auto group=groups[i].toObject();group["title"]="Ditto · focused-05000";
        group["description"]="App Store · A2E 5000";groups[i]=group;
    }
    catalog["groups"]=groups;
#endif
    return catalog;
}
QList<ModelAsset> parse(const QJsonObject &catalog) {
    QList<ModelAsset> result;
    for(const auto &v:catalog["files"].toArray()) {
        const auto f=v.toObject();
        result.append({f["group"].toString(),f["path"].toString(),f["repo"].toString(),f["revision"].toString(),
            f["file"].toString(),f["sha256"].toString(),f["resource"].toString(),f["bytes"].toInteger(),f["appleOnly"].toBool(),f["modelscopeUrl"].toString(),f["downloadBytes"].toInteger(),f["downloadSha256"].toString(),f["conversion"].toString()});
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
    static const auto files=parse(channelCatalog());return files;
}
ModelManager::ModelManager(QObject *parent,QByteArray catalog,QUrl testTransport):QObject(parent),m_testTransport(testTransport) {
    const auto data=catalog.isEmpty()?channelCatalog():QJsonDocument::fromJson(catalog).object();
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
    m_endpoint=settings.value("modelEndpoint","auto").toString();
#endif
    if(!sourceValues().contains(m_endpoint))m_endpoint=sourceValues().first();
    m_quant=qEnvironmentVariable("MOSS_IMAGE_QUANT",settings.value("imageQuant","Q4_K").toString());
    if(m_quant!="Q4_K"&&m_quant!="Q8_0")m_quant="Q4_K";
}
ModelManager::~ModelManager() {
    for(auto transport:findChildren<ModelHttpDownload *>()){transport->disconnect(this);transport->cancel();}
}
void ModelManager::setRoot(QString value) {
    if(!rootSelectable())return;
    if(locked()||value.isEmpty()||!QDir::isAbsolutePath(value))return;
    m_root=QDir::cleanPath(value);QSettings().setValue("publicModelRoot",m_root);m_error.clear();emit changed();
}
QStringList ModelManager::sourceNames() const {
#ifdef MOSS_APP_STORE
    return {"Apple · App Store 资源包", "自动选择", "国内镜像 · hf-mirror.com", "Hugging Face · huggingface.co"};
#else
    return {"自动选择", "国内镜像 · hf-mirror.com", "Hugging Face · huggingface.co"};
#endif
}
QStringList ModelManager::sourceValues() const {
#ifdef MOSS_APP_STORE
    return {"apple-assets", "auto", "https://hf-mirror.com", "https://huggingface.co"};
#else
    return {"auto", "https://hf-mirror.com", "https://huggingface.co"};
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
    qint64 bytes=0;
    for(const auto &a:m_assets)if(safePath(a.path+".part")&&safePath(a.path+".part.ranges.json"))
        bytes+=ModelHttpDownload::savedBytes(m_root+"/"+a.path+".part",a.transferBytes(),a.transferSha256())
            +QFileInfo(m_root+"/"+a.path+".part.ranges.json").size();
    return bytes;
}
QVariantList ModelManager::resources() const {
    QVariantList rows;
    const QStringList ids={"image","ditto","voice","sfx"};
    const QStringList titles={"图像创作","数字人","语音与音色","音效"};
    for(int i=0;i<ids.size();++i) {
        const auto files=selected(expand({ids[i]}));
        if(files.isEmpty())continue;
        qint64 total=0,missing=0;
        for(const auto &asset:files){total+=asset.bytes;if(!present(asset))missing+=asset.bytes;}
        rows.append(QVariantMap{{"id",ids[i]},{"title",titles[i]},
            {"ready",ready(ids[i])},{"totalBytes",total},{"missingBytes",missing},{"size",sizeText(missing)}});
    }
    return rows;
}
void ModelManager::showInitialSetup() {
    if(locked()||m_setupOffered||QSettings().value("resourceSetupVersion",0).toInt()>=1)return;
    m_setupOffered=true;
    QStringList defaults;bool missing=false;
    for(const auto &value:resources()) {
        const auto row=value.toMap();if(row["ready"].toBool())continue;
        missing=true;
        const auto id=row["id"].toString();if(id=="ditto"||id=="voice")defaults<<id;
    }
    if(!missing){QSettings().setValue("resourceSetupVersion",1);return;}
    m_initialSetup=true;setRequested(defaults);emit downloadRequested();
}
void ModelManager::setRequested(QStringList capabilities) {
    if(locked())return;
    QStringList valid;
    for(const auto &id:capabilities)if(!expand({id}).isEmpty()&&!valid.contains(id))valid<<id;
    m_requested=valid;m_error.clear();m_status.clear();m_progress=0;emit changed();
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
    if(locked())return false;
    QStringList missing;for(const auto &id:expand(capabilities))if(!ready(id)||(id=="sfx"&&!sfxTermsAccepted()))missing<<id;
    if(missing.isEmpty())return true;
    // Keep the logical image group so Q4/Q8 can be selected in the dialog.
    setRequested(capabilities);emit downloadRequested();return false;
}
void ModelManager::requestDownload(QString group) {
    if(locked()||expand({group}).isEmpty())return;
    setRequested({group});emit downloadRequested();
}
void ModelManager::dismissRequest(){
    if(busy())return;
    if(m_initialSetup){QSettings().setValue("resourceSetupVersion",1);m_initialSetup=false;}
    m_requested.clear();emit changed();
}
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
    qint64 required=0;QList<qint64> appleCopies;
    for(const auto &a:files)if(!present(a)) {
        required+=a.transferBytes()-ModelHttpDownload::savedBytes(m_root+"/"+a.path+".part",a.transferBytes(),a.transferSha256());
        if(a.resource.isEmpty()&&(a.appleOnly||m_endpoint=="apple-assets"))appleCopies<<a.bytes;
        if(!a.conversion.isEmpty())required+=a.bytes; // Atomic conversion needs an output copy.
    }
    std::sort(appleCopies.begin(),appleCopies.end(),std::greater<qint64>());
    for(int i=0;i<std::min(3,int(appleCopies.size()));++i)required+=appleCopies[i];
    QStorageInfo storage(m_root);
    if(storage.isValid()&&storage.bytesAvailable()<required+64*1024*1024){m_lock.reset();m_error="磁盘空间不足，请先清理模型或更换目录";emit changed();return;}
    m_active.clear();m_activeBytes.clear();m_stopping=false;
    m_queue=files;m_busy=true;m_cancelled=false;m_verifyOnly=false;m_error.clear();m_index=0;m_done=0;m_total=0;m_progress=0;
    for(const auto &a:m_queue)m_total+=a.bytes;emit changed();next();
}
void ModelManager::verifyGroup(QString id) {
    const auto files=selected(expand({id}));if(files.isEmpty()||!acquire())return;
    m_active.clear();m_activeBytes.clear();m_stopping=false;
    m_queue=files;m_busy=true;m_cancelled=false;m_verifyOnly=true;m_error.clear();m_index=0;m_done=0;m_total=0;m_progress=0;
    for(const auto &a:files)m_total+=a.bytes;emit changed();next();
}
void ModelManager::cancel() {
    if(!m_busy)return;
    m_cancelled=true;m_stopping=true;m_status="正在取消…";emit changed();
    for(auto transport:findChildren<ModelHttpDownload *>())transport->cancel();
#ifdef MOSS_APP_STORE
    for(auto transport:findChildren<AppleAssetDownload *>())transport->cancel();
#endif
    QTimer::singleShot(0,this,&ModelManager::next);
}
void ModelManager::finish(QString error,bool cancelled) {
    m_lock.reset();m_busy=false;m_error=error;
    m_status=cancelled?"已取消，下载残留可续传或清理":error.isEmpty()?(m_verifyOnly?"所选模型 SHA-256 校验通过":"模型已就绪，请重新点击生成"):"操作未完成";
    if(error.isEmpty()&&!cancelled)m_progress=1;
    emit changed();emit completed(error.isEmpty()&&!cancelled);
}
void ModelManager::advance(ModelAsset a) {
    m_done+=a.bytes;m_active.remove(a.path);m_activeBytes.remove(a.path);
    QTimer::singleShot(0,this,&ModelManager::next);
}
void ModelManager::fail(ModelAsset a,QString error) {
    m_active.remove(a.path);m_activeBytes.remove(a.path);
    if(!m_stopping) {
        m_error=error;m_stopping=true;
        for(auto transport:findChildren<ModelHttpDownload *>())transport->cancel();
#ifdef MOSS_APP_STORE
        for(auto transport:findChildren<AppleAssetDownload *>())transport->cancel();
#endif
    }
    QTimer::singleShot(0,this,&ModelManager::next);
}
void ModelManager::updateProgress(QString path,qint64 bytes) {
    m_activeBytes[path]=bytes;qint64 total=m_done;
    for(auto n:m_activeBytes)total+=n;
    m_progress=double(total)/std::max<qint64>(1,m_total);
    if(!m_refreshTimer.isValid()||m_refreshTimer.elapsed()>100){m_refreshTimer.start();emit changed();}
}
void ModelManager::next() {
    if(!m_busy)return;
    if(m_stopping){if(m_active.isEmpty())finish(m_error,m_cancelled);return;}
    if(m_index>=m_queue.size()&&m_active.isEmpty()){finish();return;}
    const int limit=m_verifyOnly?1:m_endpoint=="apple-assets"?3:2;
    while(!m_stopping&&m_index<m_queue.size()&&m_active.size()<limit) {
        const auto a=m_queue[m_index++];m_active.insert(a.path);prepare(a);
    }
}
void ModelManager::prepare(ModelAsset a) {
    const auto path=m_root+"/"+a.path;
    if(!safePath(a.path)||!safePath(a.path+".part")||!safePath(a.path+".part.ranges.json")){fail(a,"模型路径包含符号链接，已停止操作");return;}
    if(present(a)){checkFile(a,path,false);return;}
    if(m_verifyOnly){fail(a,"模型缺失或大小不符："+a.path);return;}
    if(!QDir().mkpath(QFileInfo(path).absolutePath())){fail(a,"无法创建模型目录");return;}
    if(!a.resource.isEmpty()) {
        const auto data=read(a.resource);
        if(data.size()!=a.bytes||QCryptographicHash::hash(data,QCryptographicHash::Sha256).toHex()!=a.sha256){fail(a,"应用内模型配置校验失败");return;}
        QSaveFile out(path);
        if(!out.open(QIODevice::WriteOnly)||out.write(data)!=data.size()||!out.commit()){fail(a,"无法写入模型配置");return;}
        advance(a);return;
    }
    const bool noticeReady=(!a.appleOnly && m_endpoint!="apple-assets") ||
        (safePath(".licenses/"+a.sha256+".txt") && QFileInfo(m_root+"/.licenses/"+a.sha256+".txt").isFile());
    if((ModelHttpDownload::savedBytes(path+".part",a.transferBytes(),a.transferSha256())==a.transferBytes() || (!a.conversion.isEmpty()&&QFileInfo(path+".part").size()==a.bytes)) && noticeReady){checkFile(a,path+".part",true);return;}
    transfer(a);
}
void ModelManager::checkFile(ModelAsset a,QString path,bool install) {
    m_status="校验 "+a.path;emit changed();
    auto watcher=new QFutureWatcher<QString>(this);
    connect(watcher,&QFutureWatcher<QString>::finished,this,[this,watcher,a,path,install]{
        const auto sha=watcher->result();watcher->deleteLater();
        if(m_stopping){advance(a);return;}
        if(sha.startsWith("conversion:")){fail(a,sha.mid(11));return;}
        if(!safePath(a.path)||!safePath(a.path+".part")||!safePath(a.path+".part.ranges.json")){fail(a,"模型路径已改变");return;}
        if(sha!=a.sha256) {
            if(m_verifyOnly){fail(a,"模型 SHA-256 不符："+a.path+"；请下载修复");return;}
            if(install){
                if(!a.conversion.isEmpty()&&QFileInfo(path).size()<a.transferBytes()){transfer(a);return;}
                QFile::remove(path);QFile::remove(ModelHttpDownload::journalPath(path));fail(a,"下载文件 SHA-256 不符，请重试："+a.path);return;
            }
            if(a.resource.isEmpty()){transfer(a);return;}
            QFile::remove(path);prepare(a);return;
        }
        if(install) {
            const auto dest=m_root+"/"+a.path;
            const auto error=installModelFile(path,dest);
            if(!error.isEmpty()){fail(a,"无法安装模型文件："+a.path+"；"+error+"。已保留下载文件，请关闭占用文件的程序后重试。");return;}
            QFile::remove(ModelHttpDownload::journalPath(path));
        }
        advance(a);
    });
    watcher->setFuture(QtConcurrent::run([path,a,install]{
        const auto sha=hashFile(path);
        if(!install||sha==a.sha256||a.conversion.isEmpty())return sha;
        if(sha!=a.transferSha256()||QFileInfo(path).size()!=a.transferBytes())return sha;
        if(a.conversion!="warp-opset20")return QString("conversion:未知模型转换格式");
        try {
            QFile input(path);if(!input.open(QIODevice::ReadOnly))return QString("conversion:无法读取 Warp 下载文件");
            const auto converted=WarpModelConversion::convert(input.readAll());input.close();
            if(converted.size()!=a.bytes||QCryptographicHash::hash(converted,QCryptographicHash::Sha256).toHex()!=a.sha256)
                return QString("conversion:Warp 转换校验失败，已保留下载文件");
            QSaveFile output(path);
            if(!output.open(QIODevice::WriteOnly)||output.write(converted)!=converted.size()||!output.commit())
                return QString("conversion:无法保存 Warp 转换，已保留下载文件");
            return a.sha256;
        } catch(const std::exception &e) {return QString("conversion:Warp 转换失败：")+QString::fromUtf8(e.what());}
    }));
}
void ModelManager::transfer(ModelAsset a) {
    const auto path=m_root+"/"+a.path+".part";
    if(!safePath(a.path+".part")||!safePath(a.path+".part.ranges.json")){fail(a,"下载路径包含符号链接");return;}
#ifdef MOSS_APP_STORE
    if(a.appleOnly || m_endpoint=="apple-assets") {
        const auto notice=".licenses/"+a.sha256+".txt";
        if(!safePath(notice)||!QDir().mkpath(m_root+"/.licenses")){fail(a,"无法保存模型许可文件");return;}
        // A sparse HTTP partial cannot be treated as an Apple copy's prefix.
        if(QFileInfo::exists(ModelHttpDownload::journalPath(path))) {
            if(!QFile::remove(path)||!QFile::remove(ModelHttpDownload::journalPath(path))){fail(a,"无法重置下载残留");return;}
        }
        auto transport=new AppleAssetDownload(this);
        m_status="正在下载…";emit changed();
        connect(transport,&AppleAssetDownload::progress,this,[this,a](double value){updateProgress(a.path,qint64(a.bytes*value));});
        connect(transport,&AppleAssetDownload::completed,this,[this,transport,a,path](QString error){
            transport->deleteLater();
            if(m_stopping){advance(a);return;}
            if(!error.isEmpty()){fail(a,"Apple 资源包未完成："+error+(a.appleOnly?"。此模型仅通过 App Store 提供，请重试。":"。可重试或手动选择镜像源。"));return;}
            checkFile(a,path,true);
        });
        transport->start("model-"+a.sha256,a.path,path,m_root+"/"+notice,a.bytes);return;
    }
#endif
    if(a.appleOnly){fail(a,"此模型仅通过 App Store 提供");return;}
    auto transport=new ModelHttpDownload(this);
    QList<QUrl> urls;
    if(!m_testTransport.isEmpty())urls<<m_testTransport.resolved(QUrl(a.file));
    else {
        // ModelScope has independent repository IDs/revisions; never substitute
        // its domain into a Hugging Face resolve URL. Explicit source choices
        // still lock the download to that provider.
        if(m_endpoint=="auto"&&!a.modelscopeUrl.isEmpty())urls<<QUrl(a.modelscopeUrl);
        const QStringList endpoints=m_endpoint=="auto"?QStringList{"https://huggingface.co"}:QStringList{m_endpoint};
        if(!a.repo.isEmpty())for(const auto &endpoint:endpoints)urls<<QUrl(endpoint+"/"+a.repo+"/resolve/"+a.revision+"/"+a.file);
    }
    m_status="正在下载…";emit changed();
    connect(transport,&ModelHttpDownload::progress,this,[this,a](qint64 bytes){updateProgress(a.path,std::min(bytes,a.bytes));});
    connect(transport,&ModelHttpDownload::completed,this,[this,transport,a,path](QString error,bool cancelled){
        transport->deleteLater();
        if(m_stopping||cancelled){advance(a);return;}
        if(!error.isEmpty()){fail(a,error+"\n文件："+a.path+"\n下载源："+m_endpoint);return;}
        checkFile(a,path,true);
    });
    transport->start(path,a.transferBytes(),a.transferSha256(),urls,a.repo+"/"+a.revision,m_endpoint=="auto");
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
    for(const auto &a:files)for(const auto &rel:{a.path,a.path+".part",a.path+".part.ranges.json"}) {
        const auto path=m_root+"/"+rel;
        if(!safePath(rel)){failures<<rel;continue;}
        if(QFileInfo::exists(path)&&(!QFileInfo(path).isFile()||!QFile::remove(path)))failures<<rel;
    }
    m_lock.reset();m_error=failures.isEmpty()?QString():"未删除的路径："+failures.join("、");m_status="模型清理完成";emit changed();
}
void ModelManager::cleanPartials() {
    if(!acquire())return;QStringList failures;
    for(const auto &a:m_assets)for(const auto &suffix:{".part",".part.ranges.json"}){const auto rel=a.path+suffix,path=m_root+"/"+rel;
        if(!safePath(rel)){failures<<rel;continue;}
        if(QFileInfo::exists(path)&&(!QFileInfo(path).isFile()||!QFile::remove(path)))failures<<rel;
    }
    m_lock.reset();m_error=failures.isEmpty()?QString():"未清理的路径："+failures.join("、");m_status="下载残留清理完成";emit changed();
}
void ModelManager::openRoot(){QDir().mkpath(m_root);QDesktopServices::openUrl(QUrl::fromLocalFile(m_root));}
