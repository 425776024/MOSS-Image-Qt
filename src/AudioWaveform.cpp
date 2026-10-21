#include "AudioWaveform.h"
#include <QAudioDecoder>
#include <QCoreApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QPainter>
#include <QTimer>
#include <algorithm>
#include <cmath>

void WaveformPeaks::append(const QAudioBuffer &buffer) {
    if(!buffer.isValid())return;
    const auto format=buffer.format();sampleRate=format.sampleRate();
    const int channels=format.channelCount();
    for(int frame=0;frame<buffer.frameCount();++frame){
        float peak=0;
        for(int channel=0;channel<channels;++channel){
            const int i=frame*channels+channel;float value=0;
            switch(format.sampleFormat()){
            case QAudioFormat::UInt8:value=(int(buffer.constData<quint8>()[i])-128)/128.f;break;
            case QAudioFormat::Int16:value=buffer.constData<qint16>()[i]/32768.f;break;
            case QAudioFormat::Int32:value=buffer.constData<qint32>()[i]/2147483648.f;break;
            case QAudioFormat::Float:value=buffer.constData<float>()[i];break;
            default:break;
            }
            if(std::isfinite(value))peak=std::max(peak,std::min(1.f,std::abs(value)));
        }
        if(frames%framesPerPeak==0){
            if(peaks.size()==4096){
                for(int i=0;i<2048;++i)peaks[i]=std::max(peaks[2*i],peaks[2*i+1]);
                peaks.resize(2048);framesPerPeak*=2;
            }
            peaks.append(0);
        }
        peaks.last()=std::max(peaks.last(),peak);++frames;
    }
}
QVector<float> WaveformPeaks::bars(int count) const {
    QVector<float> result(std::max(0,count),0);
    if(frames==0||peaks.isEmpty())return result;
    // The final partial bucket covers only its actual frames, never a stretched
    // prefix. This preserves activity at the tail of long recordings.
    for(int x=0;x<count;++x){
        const auto start=qint64(x)*frames/count,end=std::max(start+1,qint64(x+1)*frames/count);
        for(auto i=start/framesPerPeak;i<=std::min<qint64>((end-1)/framesPerPeak,peaks.size()-1);++i)
            result[x]=std::max(result[x],peaks[i]);
    }
    return result;
}
WaveformCache::WaveformCache(QObject *parent,int capacity):QObject(parent),m_cache(capacity),m_timeout(new QTimer(this)){
    m_timeout->setSingleShot(true);m_timeout->setInterval(60000);
    connect(m_timeout,&QTimer::timeout,this,[this]{complete("波形读取超时");});
}
WaveformCache *WaveformCache::instance(){static auto cache=new WaveformCache(QCoreApplication::instance());return cache;}
QString WaveformCache::key(const QUrl &source){
    if(!source.isLocalFile())return {};
    const QFileInfo file(source.toLocalFile());if(!file.isFile())return {};
    return file.canonicalFilePath()+"|"+QString::number(file.size())+"|"+QString::number(file.lastModified().toMSecsSinceEpoch());
}
void WaveformCache::request(const QUrl &source){
    const auto id=key(source);if(id.isEmpty()||m_cache.contains(id)||m_current==id)return;
    for(const auto &pending:m_queue)if(pending.first==id)return;
    m_queue.enqueue({id,source});if(m_current.isEmpty())next();
}
void WaveformCache::next(){
    if(!m_current.isEmpty()||m_queue.isEmpty())return;
    const auto job=m_queue.dequeue();m_current=job.first;m_build={};++m_decodeCount;
    m_decoder=new QAudioDecoder(this);
    connect(m_decoder,&QAudioDecoder::bufferReady,this,[this]{m_build.append(m_decoder->read());});
    connect(m_decoder,&QAudioDecoder::finished,this,[this]{complete();});
    connect(m_decoder,qOverload<QAudioDecoder::Error>(&QAudioDecoder::error),this,[this](auto){complete(m_decoder->errorString());});
    m_timeout->start();m_decoder->setSource(job.second);m_decoder->start();
}
void WaveformCache::complete(QString error){
    if(m_current.isEmpty())return;
    m_timeout->stop();auto decoder=m_decoder;m_decoder=nullptr;
    decoder->disconnect(this);decoder->stop();decoder->deleteLater();
    if(error.isEmpty()&&m_build.frames==0)error="没有可读取的音频采样";
    m_build.error=error;
    const auto id=m_current;m_current.clear();m_cache.insert(id,new WaveformPeaks(std::move(m_build)));
    emit ready(id);QTimer::singleShot(0,this,&WaveformCache::next);
}
AudioWaveform::AudioWaveform(QQuickItem *parent):QQuickPaintedItem(parent){
    setAntialiasing(true);
    connect(WaveformCache::instance(),&WaveformCache::ready,this,[this](const QString &id){if(m_active&&id==m_key)refresh();});
    connect(this,&AudioWaveform::colorsChanged,this,[this]{update();});
}
void AudioWaveform::setSource(QUrl source){if(m_source==source)return;m_source=source;m_data={};emit sourceChanged();refresh();}
void AudioWaveform::setActive(bool active){if(m_active==active)return;m_active=active;emit activeChanged();refresh();}
void AudioWaveform::setProgress(qreal progress){progress=std::clamp(progress,qreal(0),qreal(1));if(m_progress==progress)return;m_progress=progress;emit progressChanged();update();}
void AudioWaveform::refresh(){
    m_key=WaveformCache::key(m_source);
    if(!m_active||m_key.isEmpty())m_data={};
    else if(const auto data=WaveformCache::instance()->get(m_key))m_data=*data;
    else {m_data={};WaveformCache::instance()->request(m_source);}
    emit dataChanged();update();
}
void AudioWaveform::paint(QPainter *p){
    const auto values=m_data.bars(std::clamp(int(width()/3),1,1024));
    p->setPen(Qt::NoPen);
    for(int i=0;i<values.size();++i){
        const qreal x=(i+.5)*width()/values.size();
        const qreal h=std::max(qreal(1.5),std::sqrt(values[i])*(height()-4));
        p->setBrush(x<=width()*m_progress?m_playedColor:m_waveColor);
        p->drawRoundedRect(QRectF(x-.85,(height()-h)/2,1.7,h),.85,.85);
    }
    if(m_progress>0){p->setBrush(m_playedColor);p->drawRoundedRect(QRectF(std::min(width()-1.,width()*m_progress),1,1,height()-2),.5,.5);}
}
