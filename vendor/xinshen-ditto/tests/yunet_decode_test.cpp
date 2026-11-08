#include "../liveportrait/yunet_alignment.h"
#include "../liveportrait/yunet_decode.h"
#include <iostream>
#include "../onnx_session.h"
#include "../liveportrait/coreml.h"
int main(int argc,char **argv) {
  try {
    if(argc!=3&&argc!=4)throw std::runtime_error("usage: yunet_decode_tests MODEL IMAGE");
    VideoReader reader(argv[2]);Image image;if(!reader.next(image))throw std::runtime_error("image unreadable");
    auto reference=cv::FaceDetectorYN::create(argv[1],"",{640,640},.9f,.3f,5000);
    auto network=cv::dnn::readNet(argv[1]);
    Ort::Env env(ORT_LOGGING_LEVEL_WARNING,"yunet-parity");Ort::Session accelerated{nullptr};
    if(argc==4) {
      std::filesystem::path cache;
      auto options=avatar::onnxOptions(argv[1],"ditto-v2",false,argv[3],&cache);
      accelerated=Ort::Session(env,argv[1],options);
#ifdef __APPLE__
      if(std::string(argv[3])=="CoreML")lp::validateCoreMLAcceleration(cache,"face_detection_yunet_2023mar.onnx");
#endif
    }
    const double tolerance=argc==4?1.0:1e-4;
    double maxDifference=0;
    auto detect=[&](const cv::Mat &canvas) {
      std::vector<cv::String> names;for(auto kind:{"cls","obj","bbox","kps"})for(int stride:{8,16,32})names.push_back(std::string(kind)+"_"+std::to_string(stride));
      network.setInput(cv::dnn::blobFromImage(canvas));std::vector<cv::Mat> values;network.forward(values,names);
      std::map<std::string,std::vector<float>> tensors;
      for(size_t i=0;i<names.size();++i)tensors[names[i]]=std::vector<float>(values[i].ptr<float>(),values[i].ptr<float>()+values[i].total());
      if(accelerated) {
        auto blob=cv::dnn::blobFromImage(canvas);const int64_t shape[]={1,3,640,640};
        auto memory=Ort::MemoryInfo::CreateCpu(OrtArenaAllocator,OrtMemTypeDefault);
        auto input=Ort::Value::CreateTensor<float>(memory,blob.ptr<float>(),blob.total(),shape,4);
        Ort::AllocatorWithDefaultOptions allocator;auto inputName=accelerated.GetInputNameAllocated(0,allocator);
        const char *inputs[]={inputName.get()};std::vector<const char *> outputs;for(auto &name:names)outputs.push_back(name.c_str());
        auto actual=accelerated.Run(Ort::RunOptions{nullptr},inputs,&input,1,outputs.data(),outputs.size());
        for(size_t i=0;i<names.size();++i){const float *v=actual[i].GetTensorData<float>();tensors[names[i]]=std::vector<float>(v,v+actual[i].GetTensorTypeAndShapeInfo().GetElementCount());}
      }
      auto result=yunet_alignment::decode(tensors);cv::Mat expected;reference->detect(canvas,expected);
      if(result.rows!=expected.rows)throw std::runtime_error("face count differs");
      if(!result.empty())maxDifference=std::max(maxDifference,cv::norm(result,expected,cv::NORM_INF));
      if(maxDifference>tolerance)throw std::runtime_error("decoded faces differ");
      return result;
    };
    for(bool flip:{false,true}) {
      Image input=image;
      if(flip){cv::Mat bgr(input.height,input.width,CV_8UC3,input.bgr.data());cv::flip(bgr,bgr,1);}
      auto actual=yunet_alignment::initialAffine(input,detect),expected=yunet_alignment::initialAffine(input,reference);
      if(cv::norm(actual,expected,cv::NORM_INF)>tolerance)throw std::runtime_error("alignment differs");
    }
    if(!detect(cv::Mat::zeros(640,640,CV_8UC3)).empty())throw std::runtime_error("blank face detected");
    std::cout<<"max face difference="<<maxDifference<<"\n";
    std::cout<<"PASS: portrait, mirrored portrait, blank; face tensors and crop match OpenCV\n";
    return 0;
  }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
