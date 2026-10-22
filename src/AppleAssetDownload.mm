#include "AppleAssetDownload.h"
#include <QPointer>
#include <QFile>
#include <QSaveFile>
#include <atomic>
#include <fcntl.h>
#include <unistd.h>
#import <Foundation/Foundation.h>
#import <BackgroundAssets/BackgroundAssets.h>

API_AVAILABLE(macos(26))
@interface XinshenAssetJob : NSObject <BAManagedAssetPackDownloadDelegate> {
@public
    QPointer<AppleAssetDownload> owner;
    std::atomic_bool cancelled;
}
@property(nonatomic, strong) NSProgress *downloadProgress;
@property(nonatomic, copy) NSString *packID;
- (void)finish:(QString)error;
@end
@implementation XinshenAssetJob
- (void)finish:(QString)error {
    dispatch_async(dispatch_get_main_queue(), ^{
        if (self->owner) emit self->owner->completed(error);
    });
}
- (void)downloadOfAssetPack:(BAAssetPack *)pack hasProgress:(NSProgress *)progress {
    if (![pack.identifier isEqualToString:self.packID]) return;
    dispatch_async(dispatch_get_main_queue(), ^{
        self.downloadProgress=progress;
        if (self->cancelled) [progress cancel];
        else if (self->owner) emit self->owner->progress(progress.fractionCompleted);
    });
}
@end

AppleAssetDownload::~AppleAssetDownload() {
    cancel();
    if(m_job) CFRelease(m_job);
}
void AppleAssetDownload::cancel() {
    if (@available(macOS 26, *)) {
        if(m_job) {
            auto job=(__bridge XinshenAssetJob *)m_job;
            job->cancelled=true;
            [job.downloadProgress cancel];
        }
    }
}
void AppleAssetDownload::start(QString pack,QString relativePath,QString destination,QString noticeDestination,qint64 bytes) {
    if (@available(macOS 26, *)) {
        if(m_job) { CFRelease(m_job); m_job=nullptr; }
        if(![[NSBundle mainBundle] objectForInfoDictionaryKey:@"BAUsesAppleHosting"]) {
            emit completed(QStringLiteral("此安装包未配置 Apple 资源包"));return;
        }
        auto job=[XinshenAssetJob new]; job->owner=this; job->cancelled=false;
        job.packID=pack.toNSString(); m_job=(__bridge_retained void *)job;
        auto manager=BAAssetPackManager.sharedManager;
        manager.delegate=job;
        [manager getAssetPackWithIdentifier:job.packID completionHandler:^(BAAssetPack *asset,NSError *error) {
            if(error || !asset || job->cancelled) {
                [job finish:error ? QString::fromNSString(error.localizedDescription) : QStringLiteral("下载已取消")]; return;
            }
            [manager ensureLocalAvailabilityOfAssetPack:asset completionHandler:^(NSError *error) {
                if(error || job->cancelled) {
                    [job finish:error ? QString::fromNSString(error.localizedDescription) : QStringLiteral("下载已取消")]; return;
                }
                // Copy through a pack-scoped descriptor, not the merged namespace.
                // Workers consume owned container files after ModelManager checks SHA-256.
                dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY,0), ^{
                    @autoreleasepool {
                        NSError *readError=nil;
                        int fd=[manager fileDescriptorForPath:relativePath.toNSString()
                            searchingInAssetPackWithIdentifier:job.packID error:&readError];
                        QString failure;
                        if(fd<0) failure=QString::fromNSString(readError.localizedDescription ?: @"资源文件不可读");
                        else {
                            int out=::open(QFile::encodeName(destination).constData(),O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW,0600);
                            if(out<0) failure=QStringLiteral("无法写入模型缓存");
                            else {
                                QByteArray buffer(1024*1024,Qt::Uninitialized); qint64 total=0;
                                while(!job->cancelled) {
                                    ssize_t n=::read(fd,buffer.data(),buffer.size());
                                    if(n==0)break;
                                    if(n<0 || total+n>bytes){failure=QStringLiteral("资源包文件大小或读取异常");break;}
                                    ssize_t done=0;
                                    while(done<n){auto w=::write(out,buffer.data()+done,n-done);if(w<=0){failure=QStringLiteral("磁盘写入失败");break;}done+=w;}
                                    if(!failure.isEmpty())break;
                                    total+=n;
                                }
                                if(job->cancelled)failure=QStringLiteral("下载已取消");
                                if(failure.isEmpty()&&total!=bytes)failure=QStringLiteral("资源包文件大小不符");
                                ::close(out);
                            }
                            ::close(fd);
                        }
                        if(failure.isEmpty()) {
                            NSError *noticeError=nil;
                            NSData *notice=[manager contentsAtPath:[NSString stringWithFormat:@"licenses/%@/NOTICE.txt",job.packID]
                                searchingInAssetPackWithIdentifier:job.packID options:NSDataReadingMappedIfSafe error:&noticeError];
                            if(!notice || notice.length>8*1024*1024) failure=QStringLiteral("模型许可文件缺失或异常");
                            else {
                                QSaveFile out(noticeDestination);
                                if(!out.open(QIODevice::WriteOnly) || out.write((const char *)notice.bytes,notice.length)!=qint64(notice.length) || !out.commit())
                                    failure=QStringLiteral("无法保存模型许可文件");
                            }
                        }
                        // Owned copy is version-pinned. Release the system copy to avoid
                        // retaining two full sets of model weights and automatic updates.
                        [manager removeAssetPackWithIdentifier:job.packID completionHandler:^(NSError *removeError) {
                            if(removeError && failure.isEmpty())
                                [job finish:QStringLiteral("资源已复制，但系统缓存清理失败，请重试：")+QString::fromNSString(removeError.localizedDescription)];
                            else [job finish:failure];
                        }];
                    }
                });
            }];
        }];
    } else emit completed(QStringLiteral("Apple 资源包需要 macOS 26 或更新版本，请手动选择镜像源"));
}
