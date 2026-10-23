#include "ModelHttpDownload.h"
#include <QNetworkReply>
#include <QNetworkInterface>
#include <QNetworkInformation>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QFileInfo>
#include <QHash>
#include <algorithm>

namespace {
struct CachedSource { int source; QElapsedTimer age; };
QHash<QString,CachedSource> &sourceCache() { static QHash<QString,CachedSource> cache; return cache; }
QString networkKey() {
    QStringList addresses;
    for(const auto &iface:QNetworkInterface::allInterfaces())if(iface.flags().testFlag(QNetworkInterface::IsUp))
        for(const auto &entry:iface.addressEntries())addresses<<iface.name()+":"+entry.ip().toString();
    addresses.sort();return addresses.join('|');
}
QJsonObject journal(const QString &path) {
    QFile f(ModelHttpDownload::journalPath(path));
    if(!f.open(QIODevice::ReadOnly)||f.size()>16384)return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}
bool validRanges(const QJsonObject &obj,qint64 total,const QString &identity,qint64 size) {
    if(obj["identity"].toString()!=identity||obj["total"].toInteger()!=total)return false;
    const auto ranges=obj["ranges"].toArray();if(ranges.isEmpty()||ranges.size()>4)return false;
    qint64 expected=0;
    for(const auto &v:ranges) {
        const auto r=v.toObject();const auto begin=r["begin"].toInteger(-1),end=r["end"].toInteger(-1),n=r["received"].toInteger(-1);
        if(begin!=expected||end<=begin||end>total||n<0||n>end-begin||(n>0&&begin+n>size))return false;
        expected=end;
    }
    return expected==total;
}
bool contentRange(QNetworkReply *reply,qint64 begin,qint64 end,qint64 total) {
    static const QRegularExpression re("^bytes ([0-9]+)-([0-9]+)/([0-9]+)$");
    const auto match=re.match(QString::fromLatin1(reply->rawHeader("Content-Range")));
    return match.hasMatch()&&match.captured(1).toLongLong()==begin&&match.captured(2).toLongLong()==end-1&&match.captured(3).toLongLong()==total;
}
QNetworkRequest request(const QUrl &url,int timeout) {
    QNetworkRequest req(url);req.setTransferTimeout(timeout);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setRawHeader("Accept-Encoding","identity");return req;
}
}
ModelHttpDownload::ModelHttpDownload(QObject *parent):QObject(parent) {
    m_probeTimer.setSingleShot(true);
    connect(&m_probeTimer,&QTimer::timeout,this,&ModelHttpDownload::finishProbes);
    m_watchdog.setInterval(500);
    connect(&m_watchdog,&QTimer::timeout,this,[this]{
        if(!m_active||m_probing)return;
        bool receiving=false;
        for(const auto &r:m_ranges)if(r.reply) {
            receiving|=r.hasData;
            const int timeout=r.hasData?m_policy.idleMs:m_policy.firstByteMs;
            if(r.activity.elapsed()>=timeout){retry(r.hasData?"连接长时间无数据":"等待服务器响应超时");return;}
        }
        // Redirects/TLS and storage warm-up get their own first-byte budget.
        // Do not run throughput reassessment before any payload has arrived.
        if(!receiving)return;
        if(m_window.elapsed()>=m_policy.slowMs) {
            const double speed=double(received()-m_windowBytes)*1000/std::max<qint64>(1,m_window.elapsed());
            m_window.restart();m_windowBytes=received();
            if(m_auto&&m_urls.size()>1) {
                if(speed>=m_policy.slowBytesPerSecond) {
                    m_slowTried.fill(false);m_cooldown.invalidate();
                } else if(!m_cooldown.isValid()||m_cooldown.elapsed()>=m_policy.cooldownMs) {
                    if(m_cooldown.isValid()){m_cooldown.invalidate();m_slowTried.fill(false);}
                    switchSlowSource(speed);
                }
            }
        }
    });
    QNetworkInformation::loadDefaultBackend();
    static bool observingNetwork=false;
    if(auto info=QNetworkInformation::instance();info&&!observingNetwork) {
        observingNetwork=true;
        // Keep observing between jobs, when no downloader object is alive.
        connect(info,&QNetworkInformation::reachabilityChanged,info,[]{clearSourceCache();});
        connect(info,&QNetworkInformation::transportMediumChanged,info,[]{clearSourceCache();});
    }
}
ModelHttpDownload::~ModelHttpDownload(){stopReplies();if(m_active)checkpoint();}
void ModelHttpDownload::clearSourceCache(){sourceCache().clear();}
qint64 ModelHttpDownload::savedBytes(const QString &path,qint64 total,const QString &identity) {
    const qint64 size=QFileInfo(path).size();
    if(!QFileInfo::exists(journalPath(path)))return std::min(size,total);
    const auto obj=journal(path);if(!validRanges(obj,total,identity,size))return 0;
    qint64 bytes=0;for(const auto &r:obj["ranges"].toArray())bytes+=r.toObject()["received"].toInteger();return bytes;
}
bool ModelHttpDownload::restore() {
    if(QFileInfo(m_path).isSymLink()||QFileInfo(journalPath(m_path)).isSymLink())return false;
    m_file.setFileName(m_path);if(!m_file.open(QIODevice::ReadWrite))return false;
    if(QFileInfo::exists(journalPath(m_path))) {
        const auto obj=journal(m_path);
        if(!validRanges(obj,m_total,m_identity,m_file.size()))return false;
        for(const auto &v:obj["ranges"].toArray()) {const auto r=v.toObject();m_ranges.append({r["begin"].toInteger(),r["end"].toInteger(),r["received"].toInteger(),{}});}
        m_stream=obj["stream"].toBool();
        if(m_stream&&m_ranges.size()!=1)return false;
    } else {
        qint64 prefix=m_file.size();if(prefix>m_total){if(!m_file.resize(0))return false;prefix=0;}
        int count=m_total>=m_policy.parallelThreshold?std::clamp(m_policy.connections,1,4):1;
        // Divide without rounding holes, and adopt an older contiguous .part.
        for(int i=0;i<count;++i) {
            qint64 begin=m_total*i/count,end=m_total*(i+1)/count;
            m_ranges.append({begin,end,std::clamp(prefix-begin,qint64(0),end-begin),{}});
        }
    }
    return checkpoint(); // Save the layout before writing any non-contiguous bytes.
}
void ModelHttpDownload::start(QString path,qint64 bytes,QString identity,QList<QUrl> urls,QString cacheKey,bool automatic) {
    if(m_active)return;
    ++m_generation;m_source=0;m_probing=false;m_cooldown.invalidate();
    m_path=path;m_total=bytes;m_identity=identity;m_urls=urls;m_auto=automatic;m_active=true;
    m_ranges.clear();m_failures.fill(0,urls.size());m_speeds.fill(-1,urls.size());m_slowTried.fill(false,urls.size());m_stream=false;
    m_cacheKey=cacheKey+"|"+networkKey();
    for(const auto &url:urls)m_cacheKey+="|"+url.scheme()+"://"+url.authority();
    if(bytes<=0||urls.isEmpty()||!restore()){
        m_file.close(); // Do not overwrite an invalid or different-version journal.
        finish("下载断点无效或文件不可写，请清理下载残留后重试");return;
    }
    emit progress(received());
    if(!m_active)return;
    if(received()==m_total){finish();return;}
    if(m_auto&&m_urls.size()>1) {
        const auto cache=sourceCache().constFind(m_cacheKey);
        if(cache!=sourceCache().cend()&&cache->age.elapsed()<10*60*1000&&cache->source<m_urls.size())beginTransfer(cache->source);
        else probe();
    } else beginTransfer(0);
}
qint64 ModelHttpDownload::received() const {qint64 n=0;for(const auto &r:m_ranges)n+=r.received;return n;}
bool ModelHttpDownload::checkpoint() {
    if(!m_file.isOpen())return true;
    if(!m_file.flush())return false;
    QJsonArray ranges;for(const auto &r:m_ranges)ranges.append(QJsonObject{{"begin",r.begin},{"end",r.end},{"received",r.received}});
    QSaveFile f(journalPath(m_path));
    const auto data=QJsonDocument(QJsonObject{{"identity",m_identity},{"total",m_total},{"stream",m_stream},{"ranges",ranges}}).toJson(QJsonDocument::Compact);
    m_checkpoint.start();return !QFileInfo(f.fileName()).isSymLink()&&f.open(QIODevice::WriteOnly)&&f.write(data)==data.size()&&f.commit();
}
void ModelHttpDownload::probe() {
    m_watchdog.stop();m_probing=true;m_probes.clear();m_probes.resize(m_urls.size());
    m_probeTimer.start(m_policy.probeMs);
    const qint64 limit=std::min(m_total,m_policy.probeBytes);
    for(int i=0;i<m_urls.size();++i) {
        auto req=request(m_urls[i],m_policy.probeMs);req.setRawHeader("Range","bytes=0-"+QByteArray::number(limit-1));
        auto &p=m_probes[i];p.clock.start();auto reply=m_network.get(req);p.reply=reply;reply->setReadBufferSize(64*1024);
        connect(reply,&QNetworkReply::readyRead,this,[this,i,reply,limit]{
            auto &p=m_probes[i];const int status=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            p.valid=(status==206&&contentRange(reply,0,limit,m_total))||status==200;
            if(!p.valid){reply->abort();return;}
            p.bytes+=reply->read(std::max<qint64>(0,limit-p.bytes)).size();
            if(p.bytes>=limit)reply->abort(); // Also bound a server that ignores Range.
        });
        connect(reply,&QNetworkReply::finished,this,[this,i,reply]{
            auto &p=m_probes[i];if(p.valid&&p.bytes>0)p.speed=double(p.bytes)*1000/std::max<qint64>(1,p.clock.elapsed());
            p.reply=nullptr;reply->deleteLater();
            if(std::none_of(m_probes.cbegin(),m_probes.cend(),[](const Probe &v){return !v.reply.isNull();}))finishProbes();
        });
    }
}
void ModelHttpDownload::finishProbes() {
    if(!m_active||!m_probing)return;
    m_probeTimer.stop();int best=-1;double fastest=0;
    for(int i=0;i<m_probes.size();++i) {
        auto &p=m_probes[i];
        if(p.reply) {
            if(p.valid&&p.bytes>0)p.speed=double(p.bytes)*1000/std::max<qint64>(1,p.clock.elapsed());
            p.reply->disconnect(this);p.reply->abort();p.reply->deleteLater();p.reply=nullptr;
        }
        if(p.speed>fastest&&m_failures[i]<2){fastest=p.speed;best=i;}
    }
    m_probing=false;
    // A short failed probe is not proof the source cannot deliver. Try it with the
    // normal bounded timeout; a transient redirect or handshake can take longer.
    if(best<0)best=m_source;
    if(fastest>0) {
        if(sourceCache().size()>32)sourceCache().clear();
        CachedSource entry{best,{}};entry.age.start();sourceCache().insert(m_cacheKey,entry);
    }
    beginTransfer(best);
}
void ModelHttpDownload::switchSlowSource(double speed) {
    m_speeds[m_source]=speed;m_slowTried[m_source]=true;
    int next=-1;
    for(int n=1;n<m_urls.size();++n) {
        const int i=(m_source+n)%m_urls.size();
        if(!m_slowTried[i]&&m_failures[i]<2){next=i;break;}
    }
    // Each source gets a real transfer window. If all are slow, continue with
    // the fastest measured source and pause this cycle to avoid ping-pong.
    if(next<0) {
        next=m_source;
        for(int i=0;i<m_urls.size();++i)
            if(m_failures[i]<2&&m_speeds[i]>m_speeds[next])next=i;
        m_cooldown.start();
    }
    sourceCache().remove(m_cacheKey);
    if(next==m_source)return;
    qInfo()<<"Model download below speed threshold"<<qint64(speed)<<"bytes/s; switching to"<<m_urls[next].host();
    stopReplies();if(!checkpoint()){finish("无法保存下载断点");return;}
    m_source=next;const auto generation=m_generation;
    QTimer::singleShot(300,this,[this,next,generation]{if(m_active&&m_generation==generation)beginTransfer(next);});
}
void ModelHttpDownload::beginTransfer(int source) {
    if(!m_active)return;
    m_source=source;m_rangeConfirmed=false;m_window.start();m_windowBytes=received();m_watchdog.start();
    qInfo()<<"Model download source"<<m_urls[source].host()<<"ranges"<<m_ranges.size()<<"resumed bytes"<<received();
    emit sourceSelected(m_urls[source]);
    // Confirm support on the first missing range before opening other connections.
    for(int i=0;i<m_ranges.size();++i)if(m_ranges[i].received<m_ranges[i].end-m_ranges[i].begin){launch(i);return;}
    finish();
}
void ModelHttpDownload::launch(int index) {
    auto &r=m_ranges[index];if(r.reply||r.received==r.end-r.begin)return;
    const qint64 offset=r.begin+r.received;
    // Qt's inactivity timer also covers connection setup and redirects. The
    // per-range watchdog enforces the shorter idle timeout after data arrives.
    auto req=request(m_urls[m_source],std::max(m_policy.firstByteMs,m_policy.idleMs));
    if(!m_stream)req.setRawHeader("Range","bytes="+QByteArray::number(offset)+"-"+QByteArray::number(r.end-1));
    else if(offset)req.setRawHeader("Range","bytes="+QByteArray::number(offset)+"-");
    r.activity.start();r.hasData=false;
    auto reply=m_network.get(req);r.reply=reply;reply->setReadBufferSize(256*1024);reply->setProperty("offset",offset);
    connect(reply,&QNetworkReply::readyRead,this,[this,index,reply]{consume(index,reply);});
    connect(reply,&QNetworkReply::finished,this,[this,index,reply]{
        consume(index,reply);
        if(!m_active||index>=m_ranges.size()||m_ranges[index].reply!=reply)return;
        auto &r=m_ranges[index];const auto err=reply->error();const auto detail=reply->errorString();
        if(err!=QNetworkReply::NoError||r.received!=r.end-r.begin){retry("下载中断："+detail);return;}
        r.reply=nullptr;reply->deleteLater();
        if(received()==m_total)finish();
    });
}
void ModelHttpDownload::consume(int index,QNetworkReply *reply) {
    if(!m_active||index>=m_ranges.size()||m_ranges[index].reply!=reply)return;
    if(!reply->property("checked").toBool()) {
        const int status=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if(status!=200&&status!=206){reply->readAll();return;}
        if(status==200&&!m_stream){fallbackToStream();return;}
        auto &r=m_ranges[index];
        if(status==206&&!contentRange(reply,reply->property("offset").toLongLong(),r.end,m_total)){retry("服务器续传位置或文件大小不正确");return;}
        if(status==200) {
            if(!m_file.resize(0)){finish("磁盘写入失败");return;}r.received=0;
        }
        reply->setProperty("checked",true);
        if(!m_stream&&!m_rangeConfirmed) {
            m_rangeConfirmed=true;
            for(int i=0;i<m_ranges.size();++i)if(i!=index)launch(i);
        }
    }
    auto &r=m_ranges[index];const auto data=reply->readAll();if(data.isEmpty())return;
    if(data.size()>r.end-r.begin-r.received){retry("服务器返回的数据超出请求范围");return;}
    if(!m_file.seek(r.begin+r.received)||m_file.write(data)!=data.size()){finish("磁盘写入失败");return;}
    // Start throughput timing with payload, excluding redirect/startup delay.
    if(received()==m_windowBytes)m_window.restart();
    r.received+=data.size();r.activity.restart();r.hasData=true;
    if(m_checkpoint.elapsed()>1000&&!checkpoint()){finish("无法保存下载断点");return;}
    emit progress(received());
}
void ModelHttpDownload::fallbackToStream() {
    stopReplies();qint64 prefix=0;
    for(const auto &r:m_ranges){prefix+=r.received;if(r.received!=r.end-r.begin)break;}
    if(!m_file.resize(prefix)){finish("无法重置下载文件");return;}
    m_ranges={{0,m_total,prefix,{}}};m_stream=true;
    if(!checkpoint()){finish("无法保存下载断点");return;}
    beginTransfer(m_source);
}
void ModelHttpDownload::retry(QString error) {
    // Report only host/path, never a signed redirect query or credentials.
    QString host=m_urls[m_source].host();int status=0;
    for(const auto &r:m_ranges)if(r.reply) {
        status=r.reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        host=r.reply->url().host();
        if(r.reply->error()!=QNetworkReply::NoError)break;
    }
    error+=QString("\n来源：%1；响应主机：%2；HTTP %3；已保存 %4 / %5 bytes")
        .arg(m_urls[m_source].host(),host).arg(status).arg(received()).arg(m_total);
    stopReplies();sourceCache().remove(m_cacheKey);
    if(!checkpoint()){finish("无法保存下载断点");return;}
    ++m_failures[m_source];int next=-1;
    // Manual mode only has one URL. Automatic mode alternates, at most twice/source.
    for(int n=1;n<=m_urls.size();++n){int i=(m_source+n)%m_urls.size();if(m_failures[i]<2){next=i;break;}}
    if(next<0){finish(error+"，已保留断点，请稍后重试");return;}
    m_source=next;m_watchdog.stop();
    const auto generation=m_generation;
    QTimer::singleShot(300,this,[this,next,generation]{if(m_active&&m_generation==generation)beginTransfer(next);});
}
void ModelHttpDownload::stopReplies() {
    m_probeTimer.stop();m_watchdog.stop();
    for(auto &r:m_ranges)if(r.reply){r.reply->disconnect(this);r.reply->abort();r.reply->deleteLater();r.reply=nullptr;}
    for(auto &p:m_probes)if(p.reply){p.reply->disconnect(this);p.reply->abort();p.reply->deleteLater();p.reply=nullptr;}
}
void ModelHttpDownload::cancel(){if(m_active)finish({},true);}
void ModelHttpDownload::finish(QString error,bool cancelled) {
    if(!m_active)return;
    stopReplies();if(!checkpoint()&&error.isEmpty())error="无法保存下载断点";
    m_file.close();m_active=false;
    if(error.isEmpty()&&!cancelled)QFile::remove(journalPath(m_path));
    emit completed(error,cancelled);
}
