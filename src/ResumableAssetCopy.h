#pragma once
#include <QFile>
#include <QString>
#include <atomic>
#include <functional>
#include <algorithm>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

// Resume a contiguous owned copy of a pinned, locally available Apple asset.
// Verify the prefix against the pack before reusing it; never truncate on cancel.
inline QString copyAssetResuming(int source,const QString &destination,qint64 bytes,
                                const std::atomic_bool &cancelled,
                                const std::function<void(qint64)> &progress={}) {
    struct stat info{};
    if(::fstat(source,&info)!=0||info.st_size!=bytes)return "资源包文件大小不符";
    int out=::open(QFile::encodeName(destination).constData(),O_RDWR|O_CREAT|O_NOFOLLOW,0600);
    if(out<0)return "无法写入模型缓存";
    QString failure;
    if(::fstat(out,&info)!=0||!S_ISREG(info.st_mode)){::close(out);return "模型缓存不可写";}
    qint64 prefix=info.st_size;
    if(prefix>bytes)prefix=0;
    QByteArray input(1024*1024,Qt::Uninitialized),existing(1024*1024,Qt::Uninitialized);
    for(qint64 offset=0;offset<prefix&&!cancelled;) {
        const auto amount=std::min<qint64>(input.size(),prefix-offset);
        if(::pread(source,input.data(),amount,offset)!=amount||::pread(out,existing.data(),amount,offset)!=amount||memcmp(input.data(),existing.data(),amount)!=0){prefix=0;break;}
        offset+=amount;
    }
    if(!cancelled&&::ftruncate(out,prefix)!=0)failure="无法重置模型缓存";
    qint64 offset=prefix;
    while(failure.isEmpty()&&!cancelled&&offset<bytes) {
        const auto amount=std::min<qint64>(input.size(),bytes-offset);
        const auto n=::pread(source,input.data(),amount,offset);
        if(n<=0){failure="资源包读取失败";break;}
        ssize_t written=0;
        while(written<n){auto w=::pwrite(out,input.data()+written,n-written,offset+written);if(w<=0){failure="磁盘写入失败";break;}written+=w;}
        if(!failure.isEmpty())break;
        offset+=n;if(progress)progress(offset);
    }
    ::close(out);
    return cancelled?QString("下载已取消"):failure;
}
