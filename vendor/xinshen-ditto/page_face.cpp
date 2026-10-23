#include "page_face.h"
#include "media_io.h"
#include <opencv2/objdetect.hpp>
#include <opencv2/imgproc.hpp>
#include <stdexcept>

int pageFaceCount(const std::filesystem::path &source, const std::filesystem::path &models) {
    VideoReader reader(source);
    Image frame;
    if (!reader.next(frame)) throw std::runtime_error("Cannot read page image");
    cv::Mat bgr(frame.height, frame.width, CV_8UC3, frame.bgr.data()), reduced, faces;
    const double scale=std::min(1.,640./std::max(frame.width,frame.height));
    cv::resize(bgr,reduced,{},scale,scale);
    auto detector=cv::FaceDetectorYN::create(
        (models/"face_detection_yunet_2023mar.onnx").string(),"",reduced.size(),.9f,.3f,5000);
    detector->detect(reduced,faces);
    return faces.rows;
}
