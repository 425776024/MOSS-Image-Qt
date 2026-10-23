#include "AppleAssetDownload.h"
#include "ResumableAssetCopy.h"
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
    std::atomic_bool finished;
}
@property(nonatomic, strong) NSProgress *downloadProgress;
@property(nonatomic, copy) NSString *packID;
- (void)finish:(QString)error;
@end
// BAAssetPackManager has a single delegate. Route all concurrently scheduled
// pack updates through one retained dispatcher instead of overwriting it per job.
API_AVAILABLE(macos(26))
@interface XinshenAssetDispatcher : NSObject <BAManagedAssetPackDownloadDelegate>
@property(nonatomic, strong) NSMutableDictionary<NSString *,XinshenAssetJob *> *jobs;
+ (instancetype)shared;
@end
@implementation XinshenAssetDispatcher
+ (instancetype)shared {
    static XinshenAssetDispatcher *value;static dispatch_once_t once;
    dispatch_once(&once, ^{value=[XinshenAssetDispatcher new];value.jobs=[NSMutableDictionary new];});return value;
}
- (void)downloadOfAssetPack:(BAAssetPack *)pack hasProgress:(NSProgress *)progress {
    dispatch_async(dispatch_get_main_queue(), ^{
        XinshenAssetJob *job=self.jobs[pack.identifier];
        if(!job||job->finished)return;
        job.downloadProgress=progress;
        if(job->cancelled)[progress cancel];
        else if(job->owner)emit job->owner->progress(progress.fractionCompleted);
    });
}
@end
@implementation XinshenAssetJob
- (void)finish:(QString)error {
    if(finished.exchange(true))return;
    dispatch_async(dispatch_get_main_queue(), ^{
        auto dispatcher=XinshenAssetDispatcher.shared;
        if(dispatcher.jobs[self.packID]==self)[dispatcher.jobs removeObjectForKey:self.packID];
        if(self->owner)emit self->owner->completed(error);
    });
}
@end

AppleAssetDownload::~AppleAssetDownload() {
    cancel();
    if(m_job) {
        auto job=(__bridge XinshenAssetJob *)m_job;job->owner=nullptr;
        auto dispatcher=XinshenAssetDispatcher.shared;
        if(dispatcher.jobs[job.packID]==job)[dispatcher.jobs removeObjectForKey:job.packID];
        CFRelease(m_job);
    }
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
        auto job=[XinshenAssetJob new]; job->owner=this; job->cancelled=false;job->finished=false;
        job.packID=pack.toNSString(); m_job=(__bridge_retained void *)job;
        auto manager=BAAssetPackManager.sharedManager;
        auto dispatcher=XinshenAssetDispatcher.shared;
        dispatcher.jobs[job.packID]=job;manager.delegate=dispatcher;
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
                            failure=copyAssetResuming(fd,destination,bytes,job->cancelled,[job,bytes](qint64 copied){
                                dispatch_async(dispatch_get_main_queue(), ^{
                                    if(job->owner&&!job->cancelled&&!job->finished)emit job->owner->progress(double(copied)/bytes);
                                });
                            });
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
                        // Keep the system pack after cancellation or copy failure so
                        // retry can reuse it. BackgroundAssets owns network resume.
                        if(!failure.isEmpty()||job->cancelled){[job finish:failure.isEmpty()?QStringLiteral("下载已取消"):failure];return;}
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
