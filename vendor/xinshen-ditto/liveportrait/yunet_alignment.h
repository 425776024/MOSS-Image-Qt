#pragma once
// Adapted from Xinshen native/liveportrait/inference.cpp's yunet-203-v1 contract.
// YuNet weights: OpenCV Zoo 47534e27c9851bb1128ccc0102f1145e27f23f98, MIT.
#include <opencv2/objdetect.hpp>
#include <opencv2/imgproc.hpp>
#include <cmath>
#include <stdexcept>
#include "../media_io.h"

namespace yunet_alignment {
inline cv::Mat initialAffine(const Image &image, const cv::Ptr<cv::FaceDetectorYN> &detector) {
  if (image.width <= 0 || image.height <= 0) throw std::runtime_error("人像尺寸无效");
  cv::Mat bgr(image.height, image.width, CV_8UC3, const_cast<uint8_t *>(image.bgr.data()));
  const float ratio = std::min(640.f / image.width, 640.f / image.height);
  cv::Mat resized, canvas = cv::Mat::zeros(640,640,CV_8UC3), faces;
  cv::resize(bgr,resized,{int(std::round(image.width*ratio)),int(std::round(image.height*ratio))});
  resized.copyTo(canvas(cv::Rect(0,0,resized.cols,resized.rows)));
  detector->detect(canvas,faces);
  if (faces.empty()) throw std::runtime_error("未检测到人脸");
  if (faces.cols != 15 || faces.type() != CV_32F || !cv::checkRange(faces))
    throw std::runtime_error("YuNet 检测结果无效");
  int best = -1; float area = 0; cv::Rect2f box;
  for (int i=0;i<faces.rows;++i) {
    const float *r=faces.ptr<float>(i);
    auto b=cv::Rect2f(r[0]/ratio,r[1]/ratio,r[2]/ratio,r[3]/ratio) & cv::Rect2f(0,0,image.width,image.height);
    if (b.width>=8 && b.height>=8 && b.area()>area) { best=i; area=b.area(); box=b; }
  }
  if (best<0) throw std::runtime_error("人物区域过小");
  const float *r=faces.ptr<float>(best);
  auto point=[&](int i){return cv::Point2f(r[4+i*2]/ratio,r[5+i*2]/ratio);};
  auto uy=(point(3)+point(4)-point(0)-point(1))*.5f;
  const float distance=cv::norm(uy);
  if (!std::isfinite(distance) || distance<2) throw std::runtime_error("关键点几何退化");
  uy*=1.f/distance;
  const cv::Point2f ux(uy.y,-uy.x);
  const float side=std::max(box.width,box.height)*1.5f;
  auto center=box.tl()+cv::Point2f(box.width*.5f,box.height*.5f)-uy*(side*.1f);
  const float scale=224.f/side;
  return (cv::Mat_<double>(2,3) << scale*ux.x,scale*ux.y,112.-scale*center.dot(ux),
      scale*uy.x,scale*uy.y,112.-scale*center.dot(uy));
}
}
