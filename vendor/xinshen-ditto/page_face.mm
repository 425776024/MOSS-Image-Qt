#include "page_face.h"
#include <stdexcept>
#import <Vision/Vision.h>

int pageFaceCount(const std::filesystem::path &image) {
    @autoreleasepool {
        NSURL *url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:image.string().c_str()]];
        VNImageRequestHandler *handler = [[VNImageRequestHandler alloc] initWithURL:url options:@{}];
        VNDetectFaceRectanglesRequest *request = [[VNDetectFaceRectanglesRequest alloc] init];
        NSError *error = nil;
        if (![handler performRequests:@[request] error:&error])
            throw std::runtime_error(std::string("人像检测失败：") + (error.localizedDescription.UTF8String ?: "未知错误"));
        return static_cast<int>(request.results.count);
    }
}
