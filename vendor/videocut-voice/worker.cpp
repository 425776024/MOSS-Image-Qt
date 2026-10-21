#include "TtsBackend.h"
#include "VoiceTextFrontend.h"
#include "NativeTensor.h"
#include "../xinshen-ditto/media_io.h"
#include "../xinshen-ditto/avatar_runtime.h"
#include <fstream>
#include <iostream>
#include <cstring>
#include <cmath>
namespace fs=std::filesystem;
namespace ai=videocut::ai;
namespace native=videocut::ai::native;
namespace detail=videocut::ai::native::detail;

static void writeWave(const fs::path &path,const std::vector<float> &samples) {
    std::ofstream out(path,std::ios::binary);
    auto u16=[&](uint16_t v){out.put(char(v));out.put(char(v>>8));};
    auto u32=[&](uint32_t v){u16(v);u16(v>>16);};
    out.write("RIFF",4);u32(36+samples.size()*2);out.write("WAVEfmt ",8);u32(16);
    u16(1);u16(1);u32(24000);u32(48000);u16(2);u16(16);out.write("data",4);u32(samples.size()*2);
    for(float s:samples)u16(static_cast<uint16_t>(static_cast<int16_t>(std::lround(std::clamp(s,-1.f,1.f)*32767))));
    if(!out)throw std::runtime_error("无法写入合成语音");
}
int main(int argc,char **argv) {
    try {
        if(argc!=2)throw std::runtime_error("usage: moss-voice request.json");
        avatar::installCancellation();
        const auto request=avatar::readJson(argv[1]);
        const fs::path models=request.at("models").get<std::string>();
        const fs::path output=request.at("output").get<std::string>();
        const auto reference=decodeAudio16k(request.at("reference").get<std::string>());
        if(reference.size()<16000*3||reference.size()>16000*30)throw std::runtime_error("音色参考录音需要 3–30 秒");
        const std::string text=request.at("text"),referenceText=request.at("referenceText");
        if(text.empty()||referenceText.empty())throw std::runtime_error("请输入文案和参考音频对应的文字");
        ai::model::ResolvedModel model;model.spec.task=ai::AiTask::Tts;
        for(const auto &[role,file]:std::vector<std::pair<std::string,std::string>>{
            {"tts.tokenizer","tokenizer.json"},{"tts.encoder","encoder.onnx"},
            {"tts.language_model","language-model.onnx"},{"tts.decoder","decoder.onnx"},
            {"text.tn_tagger_fst","zh_tn_tagger.fst"},{"text.tn_verbalizer_fst","zh_tn_verbalizer.fst"}}){
            if(!fs::is_regular_file(models/file))throw std::runtime_error("语音模型文件缺失："+file);
            model.files.push_back({role,models/file});
        }
        native::NativeSdkOptions options;options.inferenceThreads=4;options.ttsMaximumTokens=1024;
        std::cout<<avatar::Json{{"event","progress"},{"stage","voice-load"},{"done",0},{"total",1}}.dump()<<std::endl;
        // Same Hojo backend and text normalization as VideoCut-Qt. CPU is a
        // deterministic supported path; no hardware acceleration is claimed.
        auto created=detail::CreateTtsBackendFactory(ai::ComputeBackend::Cpu,options)->CreateSession(model);
        if(!created.ok())throw std::runtime_error(created.error().message);
        auto session=created.takeValue();
        detail::VoiceTextFrontend frontend(models/"tokenizer.json");
        auto chunks=frontend.PlanTargetChunks(text,120);
        std::vector<float> assembled;
        for(std::size_t i=0;i<chunks.size();++i){
            avatar::checkCancelled();
            ai::InferenceRequest input;input.requestId="moss-page-voice";
            input.cancellationRequested=[](){try{avatar::checkCancelled();return false;}catch(...){return true;}};
            input.inputs={detail::MakeUtf8Output(native::kText,chunks[i].text),
                detail::MakeUtf8Output(native::kPromptText,referenceText),
                detail::MakeFloatOutput(native::kPromptAudio,{static_cast<int64_t>(reference.size())},reference),
                detail::MakeInt32Output(native::kPromptSampleRate,{1},{16000})};
            auto result=session->RunBatch({input});
            if(!result.ok()){
                if(result.error().message.find(native::kTtsEosLimitErrorPrefix)!=std::string::npos&&chunks[i].text.size()>12){
                    auto halves=frontend.BisectTargetChunk(chunks[i].text);
                    chunks[i]=std::move(halves.first);chunks.insert(chunks.begin()+i+1,std::move(halves.second));--i;continue;
                }
                throw std::runtime_error(result.error().message);
            }
            for(const auto &tensor:result.value().front().outputs)if(tensor.name==native::kTtsWaveform){
                const auto count=tensor.bytes->size()/sizeof(float);const auto offset=assembled.size();
                assembled.resize(offset+count);std::memcpy(assembled.data()+offset,tensor.bytes->data(),tensor.bytes->size());
            }
            avatar::progress("voice-synthesis",i+1,chunks.size());
        }
        if(assembled.empty())throw std::runtime_error("语音模型未生成有效音频");
        fs::create_directories(output.parent_path());writeWave(output,assembled);
        std::cout<<avatar::Json{{"event","complete"},{"output",output.string()},{"durationSeconds",double(assembled.size())/24000}}.dump()<<std::endl;
    }catch(const std::exception &e){
        std::cerr<<avatar::Json{{"event","error"},{"message",e.what()}}.dump()<<std::endl;return 1;
    }
}
