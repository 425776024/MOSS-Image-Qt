#pragma once
// YuNet 2023 output contract: OpenCV modules/objdetect/src/face_detect.cpp.
// Independent tensor decoding for the accelerated ONNX Runtime session.
#include <opencv2/core.hpp>
#include <opencv2/dnn.hpp>
#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
namespace yunet_alignment {
inline cv::Mat decode(const std::map<std::string,std::vector<float>> &outputs) {
  cv::Mat candidates(0,15,CV_32F);
  for(int stride:{8,16,32}) {
    const int width=640/stride, count=width*width;
    auto tensor=[&](const char *name,int channels)->const std::vector<float> & {
      const auto &v=outputs.at(std::string(name)+"_"+std::to_string(stride));
      if(v.size()!=size_t(count*channels))throw std::runtime_error("YuNet output shape mismatch");
      for(float x:v)if(!std::isfinite(x))throw std::runtime_error("YuNet output is not finite");
      return v;
    };
    const auto &classes=tensor("cls",1), &objects=tensor("obj",1), &boxes=tensor("bbox",4), &points=tensor("kps",10);
    for(int cell=0;cell<count;++cell) {
      const float score=std::sqrt(std::clamp(classes[cell],0.f,1.f)*std::clamp(objects[cell],0.f,1.f));
      if(score<.9f)continue;
      const int x=cell%width,y=cell/width;
      const float w=std::exp(boxes[4*cell+2])*stride,h=std::exp(boxes[4*cell+3])*stride;
      cv::Mat row(1,15,CV_32F);float *r=row.ptr<float>();
      r[0]=(x+boxes[4*cell])*stride-w*.5f;r[1]=(y+boxes[4*cell+1])*stride-h*.5f;r[2]=w;r[3]=h;r[14]=score;
      for(int k=0;k<5;++k){r[4+2*k]=(x+points[10*cell+2*k])*stride;r[5+2*k]=(y+points[10*cell+2*k+1])*stride;}
      if(!cv::checkRange(row))throw std::runtime_error("YuNet decoded geometry is not finite");
      candidates.push_back(row);
    }
  }
  if(candidates.rows<2)return candidates;
  std::vector<cv::Rect> boxes;std::vector<float> scores;std::vector<int> keep;
  for(int i=0;i<candidates.rows;++i){const float *r=candidates.ptr<float>(i);boxes.emplace_back(int(r[0]),int(r[1]),int(r[2]),int(r[3]));scores.push_back(r[14]);}
  cv::dnn::NMSBoxes(boxes,scores,.9f,.3f,keep,1.f,5000);
  cv::Mat result(0,15,CV_32F);for(int i:keep)result.push_back(candidates.row(i));return result;
}
}
