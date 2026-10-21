#include "AudioWaveform.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QDataStream>
#include <QProcess>
#include <QStandardPaths>
#include <cmath>

class AudioWaveformTest : public QObject {
    Q_OBJECT
    static QAudioFormat format(){QAudioFormat f;f.setSampleRate(16000);f.setChannelCount(2);f.setSampleFormat(QAudioFormat::Int16);return f;}
    static QByteArray pcm(int frames){
        QByteArray bytes(frames*4,Qt::Uninitialized);auto data=reinterpret_cast<qint16*>(bytes.data());
        for(int i=0;i<frames;++i){
            const auto sample=qint16((i<frames/5||i>frames*9/10)?std::sin(i*.13)*24000:0);
            data[2*i]=sample;data[2*i+1]=-sample;
        }return bytes;
    }
    static void wave(QString path,int frames=16000){
        QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));QDataStream out(&file);out.setByteOrder(QDataStream::LittleEndian);
        const auto bytes=pcm(frames);
        out.writeRawData("RIFF",4);out<<quint32(36+bytes.size());out.writeRawData("WAVEfmt ",8);out<<quint32(16)<<quint16(1)<<quint16(2)<<quint32(16000)<<quint32(64000)<<quint16(4)<<quint16(16);out.writeRawData("data",4);out<<quint32(bytes.size());out.writeRawData(bytes.data(),bytes.size());
    }
private slots:
    void longSignalKeepsTailAndOppositePhaseStereo(){
        const int frames=16000*181+117;const auto bytes=pcm(frames);WaveformPeaks peaks;
        for(int i=0;i<frames;i+=8000)peaks.append(QAudioBuffer(bytes.mid(i*4,std::min(8000,frames-i)*4),format()));
        QCOMPARE(peaks.frames,qint64(frames));QVERIFY(peaks.peaks.size()<=4096);
        QVERIFY(std::abs(peaks.seconds()-double(frames)/16000)<.0001);
        const auto bars=peaks.bars(160);QVERIFY(bars.first()>.7);QCOMPARE(bars[80],0.f);QVERIFY(bars.last()>.7);
    }
    void floatSamplesAreFiniteAndBounded(){
        auto f=format();f.setSampleFormat(QAudioFormat::Float);f.setChannelCount(1);
        const float samples[]={NAN,INFINITY,-INFINITY,-.5f,1.8f};
        WaveformPeaks peaks;peaks.append(QAudioBuffer(QByteArray(reinterpret_cast<const char*>(samples),sizeof(samples)),f));
        QCOMPARE(peaks.frames,5);QCOMPARE(peaks.peaks.first(),1.f);
    }
    void realDecoderCacheCoalescesInvalidatesAndEvicts(){
        QTemporaryDir dir;QStringList files;for(int i=0;i<3;++i){files<<dir.filePath(QString::number(i)+".wav");wave(files.last(),16000+i*100);}
        WaveformCache cache(nullptr,2);const auto source=QUrl::fromLocalFile(files[0]);const auto key=cache.key(source);
        cache.request(source);cache.request(source);QTRY_VERIFY_WITH_TIMEOUT(cache.get(key),15000);
        QCOMPARE(cache.decodeCount(),1);QVERIFY2(cache.get(key)->error.isEmpty(),qPrintable(cache.get(key)->error));
        QVERIFY(cache.get(key)->bars(100).last()>.7);QCOMPARE(cache.get(key)->seconds(),1.);
        cache.request(source);QCOMPARE(cache.decodeCount(),1);
        for(int i=1;i<3;++i){const auto url=QUrl::fromLocalFile(files[i]);cache.request(url);QTRY_VERIFY_WITH_TIMEOUT(cache.get(cache.key(url)),15000);}
        QCOMPARE(cache.entryCount(),2);QVERIFY(!cache.get(key));
        wave(files[0],32000);const auto updated=cache.key(source);QVERIFY(updated!=key);
        cache.request(source);QTRY_VERIFY_WITH_TIMEOUT(cache.get(updated),15000);QCOMPARE(cache.get(updated)->seconds(),2.);
        QCOMPARE(cache.entryCount(),2);
    }
    void realLongDecodeKeepsFullDuration(){
        QTemporaryDir dir;const auto path=dir.filePath("long.wav");wave(path,16000*181+117);
        WaveformCache cache;const auto url=QUrl::fromLocalFile(path);cache.request(url);
        QTRY_VERIFY_WITH_TIMEOUT(cache.get(cache.key(url)),15000);
        const auto result=cache.get(cache.key(url));QVERIFY(result->error.isEmpty());QVERIFY(result->peaks.size()<=4096);
        QVERIFY(result->seconds()>181&&result->seconds()<182);QVERIFY(result->bars(160).last()>.7);QCOMPARE(result->bars(160)[80],0.f);
    }
    void compressedAudioAndInvalidFiles(){
        QTemporaryDir dir;const auto wav=dir.filePath("source.wav");wave(wav,48000);
        const auto ffmpeg=QStandardPaths::findExecutable("ffmpeg");QVERIFY2(!ffmpeg.isEmpty(),"ffmpeg is required for compressed-audio acceptance");
        for(const auto &extension:{"mp3","m4a"}){
            const auto path=dir.filePath(QString("voice.")+extension);
            QCOMPARE(QProcess::execute(ffmpeg,{"-v","error","-i",wav,"-y",path}),0);
            WaveformCache cache;const auto url=QUrl::fromLocalFile(path);cache.request(url);
            QTRY_VERIFY_WITH_TIMEOUT(cache.get(cache.key(url)),15000);
            const auto result=cache.get(cache.key(url));QVERIFY2(result->error.isEmpty(),qPrintable(result->error));
            QVERIFY(result->seconds()>2.8&&result->seconds()<3.2);QVERIFY(result->bars(100).last()>.1);
        }
        const auto bad=dir.filePath("invalid.wav");QFile f(bad);QVERIFY(f.open(QIODevice::WriteOnly));f.write("not audio");f.close();
        WaveformCache cache;const auto url=QUrl::fromLocalFile(bad);cache.request(url);QTRY_VERIFY_WITH_TIMEOUT(cache.get(cache.key(url)),15000);QVERIFY(!cache.get(cache.key(url))->error.isEmpty());
    }
};
QTEST_GUILESS_MAIN(AudioWaveformTest)
#include "audio_waveform_tests.moc"
