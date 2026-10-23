#include <audiocpp.h>
#include "../xinshen-ditto/media_io.h"
#include "../xinshen-ditto/avatar_runtime.h"
#include <fstream>
#include <iostream>
#include <cmath>
#include <memory>
#include <set>
#include <cstdlib>
namespace fs=std::filesystem;

static void check(audiocpp_status status) {
    if(status!=AUDIOCPP_OK)throw std::runtime_error(audiocpp_last_error());
}
template<class T,void (*Free)(T*)> using Owner=std::unique_ptr<T,decltype(Free)>;
static void writeWave(const fs::path &path,const float *samples,size_t frames) {
    std::ofstream out(path,std::ios::binary|std::ios::trunc);
    auto u16=[&](uint16_t v){out.put(char(v));out.put(char(v>>8));};
    auto u32=[&](uint32_t v){u16(v);u16(v>>16);};
    out.write("RIFF",4);u32(36+frames*2);out.write("WAVEfmt ",8);u32(16);
    u16(1);u16(1);u32(44100);u32(88200);u16(2);u16(16);out.write("data",4);u32(frames*2);
    for(size_t i=0;i<frames;++i)u16(static_cast<uint16_t>(static_cast<int16_t>(std::lround(std::clamp(samples[i],-1.f,1.f)*32767))));
    out.flush();if(!out)throw std::runtime_error("无法写入合成语音");
}
int main(int argc,char **argv) {
    fs::path partial;
    try {
        if(argc!=2)throw std::runtime_error("usage: moss-voice request.json");
        avatar::installCancellation();
        const auto request=avatar::readJson(argv[1]);
        const fs::path models=request.at("models").get<std::string>();
        const fs::path output=request.at("output").get<std::string>();
        const auto modelFile=models/"ming-3.0-tts-0.5b-q8_0.gguf";
        if(!fs::is_regular_file(modelFile))throw std::runtime_error("Ming Q8 语音模型缺失，请在模型中心下载");
        const auto mode=request.value("mode",std::string("clone"));
        const bool design=mode=="design";
        if(mode!="clone"&&!design)throw std::runtime_error("无效的 Ming 语音模式");
        const std::string text=request.at("text");
        const std::string language=request.value("language",std::string("zh"));
        const std::string emotion=request.value("emotion",std::string("中性"));
        const std::string dialect=request.value("dialect",std::string("standard"));
        const std::string description=request.value("description",std::string());
        if(text.empty()||text.size()>65536)throw std::runtime_error("请输入有效且长度适中的配音文案");
        if(!std::set<std::string>{"zh","en","ja","ko","de","fr","es"}.count(language))
            throw std::runtime_error("Ming 不支持该目标语言");
        if(!std::set<std::string>{"中性","高兴","悲伤","愤怒","恐惧","厌恶","惊讶"}.count(emotion)||
           !std::set<std::string>{"standard","chuanyu","cantonese"}.count(dialect))
            throw std::runtime_error("无效的情绪或方言选项");
        if(dialect!="standard"&&emotion!="中性")throw std::runtime_error("方言和情绪是独立任务，请分别生成");
        if(design&&(description.empty()||description.size()>4096||request.contains("emotion")||dialect!="standard"))
            throw std::runtime_error("语音设计需要声音描述；保存音色后可选择情绪或方言朗读");
        const bool dialectMode=!design&&dialect!="standard";
        const bool emotionMode=!design&&!dialectMode&&request.contains("emotion");
        const bool structured=dialectMode||emotionMode;
        std::vector<float> reference;std::string referenceText;
        if(!design){
            reference=decodeAudio16k(request.at("reference").get<std::string>());
            referenceText=request.value("referenceText",std::string());
            if(reference.size()<16000*3||reference.size()>16000*30||
               !std::all_of(reference.begin(),reference.end(),[](float v){return std::isfinite(v);}))
                throw std::runtime_error("音色参考录音需要 3–30 秒有效音频");
            if(!structured&&(referenceText.empty()||referenceText.size()>8192))
                throw std::runtime_error("请输入参考音频对应的文字");
        }
        const std::string task=design?"voice_design":dialectMode?"dialect":emotionMode?"emotion":language=="zh"||language=="en"?"tts":"multilingual";
        avatar::progress("voice-load",0,1);
        audiocpp_registry *registryRaw=nullptr;
        check(audiocpp_registry_create(nullptr,&registryRaw));
        Owner<audiocpp_registry,audiocpp_registry_free> registry(registryRaw,audiocpp_registry_free);
        const audiocpp_model_config config{"ming3_tts",nullptr,nullptr,nullptr};
        audiocpp_model *modelRaw=nullptr;
        check(audiocpp_model_load(registry.get(),modelFile.string().c_str(),&config,nullptr,&modelRaw));
        Owner<audiocpp_model,audiocpp_model_free> model(modelRaw,audiocpp_model_free);
        audiocpp_session *sessionRaw=nullptr;
        // Explicit GPU candidates: audio.cpp's "best" may silently select CPU.
        // This runtime has no Core ML backend; on macOS use its native Metal path.
#ifdef __APPLE__
        const std::vector<const char*> candidates={"metal"};
#else
        const std::vector<const char*> candidates={"cuda","hip","vulkan"};
#endif
        std::string selectedBackend;
        for(const auto candidate:candidates){
            const audiocpp_backend_config backend{candidate,0,4};
            const auto status=audiocpp_session_create(model.get(),design?"vdes":"clon","offline",&backend,nullptr,&sessionRaw);
            if(status==AUDIOCPP_OK){selectedBackend=candidate;break;}
            const auto reason=std::string(audiocpp_last_error());
            if(sessionRaw){audiocpp_session_free(sessionRaw);sessionRaw=nullptr;}
            std::cerr<<avatar::Json{{"event","backend-unavailable"},{"backend",candidate},{"message",reason}}.dump()<<std::endl;
        }
        if(!sessionRaw)throw std::runtime_error("语音 GPU 加速不可用，请更新显卡驱动或应用后重试；详细原因见诊断日志");
        Owner<audiocpp_session,audiocpp_session_free> session(sessionRaw,audiocpp_session_free);
        std::cout<<avatar::Json{{"event","backend"},{"component","voice"},{"backend",selectedBackend},
            {"gpu","yes"},{"threads",4},{"evidence","session-created; GPU utilization not measured"}}.dump()<<std::endl;
        Owner<audiocpp_request,audiocpp_request_free> input(audiocpp_request_create(),audiocpp_request_free);
        if(!input)throw std::runtime_error("Ming request allocation failed");
        check(audiocpp_request_set_text(input.get(),text.c_str(),structured?nullptr:language.c_str()));
        const auto option=[&](const char *key,const std::string &value){check(audiocpp_request_set_option(input.get(),key,value.c_str()));};
        option("template_name",task);option("seed","1895");option("max_tokens","512");
        if(design){option("instruct",description);option("zero_speaker_embedding","true");}
        else{
            if(dialectMode)option("dialect",dialect=="chuanyu"?"chuanyu dialect":"cantonese");
            else if(emotionMode)option("emotion",emotion);
            else option("reference_text",referenceText);
            check(audiocpp_request_set_voice_audio(input.get(),reference.data(),reference.size(),16000,1));
        }
        avatar::progress("voice-synthesis",0,1);
        // audio.cpp owns Ming's tag-aware long-text chunking. A running offline
        // call cannot be interrupted in the ABI; the parent can kill this worker.
        audiocpp_result *resultRaw=nullptr;
        const auto status=audiocpp_session_run(session.get(),input.get(),&resultRaw);
        Owner<audiocpp_result,audiocpp_result_free> result(resultRaw,audiocpp_result_free);
        check(status);avatar::checkCancelled();
        const float *samples=nullptr;size_t frames=0;int rate=0,channels=0;
        check(audiocpp_result_audio(result.get(),&samples,&frames,&rate,&channels));
        if(!samples||!frames||rate!=44100||channels!=1||frames>300U*44100U||
           !std::all_of(samples,samples+frames,[](float v){return std::isfinite(v);}))
            throw std::runtime_error("Ming 未生成有效音频，或生成超过五分钟");
        fs::create_directories(fs::absolute(output).parent_path());
        partial=output.string()+".partial";writeWave(partial,samples,frames);
        avatar::checkCancelled();fs::rename(partial,output);partial.clear();
        std::cout<<avatar::Json{{"event","complete"},{"output",output.string()},
            {"durationSeconds",double(frames)/44100},{"sampleRate",44100},{"engine","ming-3.0-q8"},{"task",task},{"emotion",emotion},{"dialect",dialect}}.dump()<<std::endl;
    }catch(const std::exception &e){
        if(!partial.empty()){std::error_code ec;fs::remove(partial,ec);}
        std::cerr<<avatar::Json{{"event","error"},{"message",e.what()}}.dump()<<std::endl;return 1;
    }
}
