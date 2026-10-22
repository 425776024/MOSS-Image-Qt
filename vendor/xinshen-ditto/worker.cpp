#include "liveportrait/ditto.h"
#include "media_io.h"
#include "page_face.h"
#include "timeline_render.h"
#include <cmath>
#include <iostream>
#include <fstream>
namespace fs=std::filesystem;
int main(int argc,char **argv){
    try{
        avatar::installCancellation();
        if(argc==3&&std::string(argv[1])=="capabilities"){
            std::cout<<ditto::capabilities(argv[2]).dump()<<std::endl;return 0;
        }
        if((argc==3||argc==4)&&std::string(argv[1])=="render-timeline"){
            if(argc==4&&std::string(argv[2])!="--request")throw std::runtime_error("usage: moss-ditto render-timeline [--request] request.json");
            renderPageTimeline(avatar::readJson(argv[argc-1]));return 0;
        }
        if(argc!=3||(std::string(argv[1])!="generate"&&std::string(argv[1])!="generate-page"))
            throw std::runtime_error("usage: moss-ditto generate[-page] request.json | render-timeline [--request] request.json | capabilities MODEL_DIRECTORY");
        const bool pageMode=std::string(argv[1])=="generate-page";
        const auto request=avatar::readJson(argv[2]);
        fs::path models=request.at("models").get<std::string>();
        fs::path image=request.at("image").get<std::string>(),audio=request.at("audio").get<std::string>();
        fs::path out=request.at("directory").get<std::string>();
        const auto mouthReference=request.value("mouthReference",std::string("auto"));
        if(mouthReference!="auto"&&mouthReference!="source")throw std::runtime_error("不支持的口型适配模式");
        if(!fs::is_regular_file(image)||!fs::is_regular_file(audio))throw std::runtime_error("人像或音频不存在");
        if(fs::exists(out/"result.json"))throw std::runtime_error("输出已存在");
        fs::create_directories(out/"avatar");
        auto source=out/"avatar"/("source"+image.extension().string());
        fs::copy_file(image,source,fs::copy_options::overwrite_existing);
        avatar::writeJson(out/"avatar"/"avatar.json",{{"kind","image"},{"source",source.filename().string()},{"sourceSha256",avatar::sha256(source)}});
        auto pcm=decodeAudio16k(audio);
        size_t count=pcm.size();
        const double limit=request.value("limitSeconds",0.);
        if(limit>0)count=std::min(count,size_t(limit*16000));
        if(count==0||count>16000*600)throw std::runtime_error("请使用十分钟以内的有效音频");
        pcm.resize(count);
        auto mixed=pcm;
        const auto effectPath=request.value("sfx",std::string());
        if(!effectPath.empty()){
            const auto effect=decodeAudio16k(effectPath);
            const float gain=std::clamp(request.value("sfxGain",.35),0.,1.);
            for(size_t n=0;n<std::min(effect.size(),mixed.size());++n)mixed[n]=std::clamp(mixed[n]+effect[n]*gain,-1.f,1.f);
        }else if(!request.value("speech",true)){
            const float gain=std::clamp(request.value("sfxGain",.35),0.,1.);
            for(auto &sample:mixed)sample*=gain;
        }
        int faces=-1;
        if(pageMode){
            faces=request.value("speech",true)?pageFaceCount(source,models):0;
            const auto route=faces>0?"portrait":"still";
            std::cout<<avatar::Json{{"event","page-route"},{"route",route},{"faces",faces}}.dump()<<std::endl;
            if(faces==0){
                VideoReader reader(source);Image frame;
                if(!reader.next(frame))throw std::runtime_error("无法读取页面画面");
                // H.264 requires even dimensions. Pad only the final row/column.
                if(frame.width%2||frame.height%2){
                    Image padded;padded.width=(frame.width+1)/2*2;padded.height=(frame.height+1)/2*2;
                    padded.bgr.assign(padded.width*padded.height*3,255);
                    for(int y=0;y<frame.height;++y)
                        std::copy_n(frame.pixel(0,y),frame.width*3,padded.pixel(0,y));
                    frame=std::move(padded);
                }
                const auto output=out/"page.mp4";
                VideoWriter writer(output,mixed,frame.width,frame.height,true,true,25,1);
                const int frames=static_cast<int>((count*25+15999)/16000);
                for(int n=0;n<frames;++n){
                    avatar::checkCancelled();writer.write(frame);
                    if(n%25==0)avatar::progress("page-video",n,frames);
                }
                writer.finish();
                avatar::writeJson(out/"result.json",{{"path",output.string()},{"route","still"},{"faces",0},{"frames",frames},{"durationSeconds",double(count)/16000}});
                std::cout<<avatar::Json{{"event","complete"},{"result",(out/"result.json").string()}}.dump()<<std::endl;
                return 0;
            }
            if(!fs::exists(models/"manifest.json"))
                throw std::runtime_error("检测到人像，请在模型中心准备 Ditto 模型后生成说话视频");
        }
        avatar::writeJson(out/"plan.json",{
            {"version",1},{"totalSamples",count},{"fpsNum",25},{"fpsDen",1},{"seed",request.value("seed",1024)},
            {"includeAudio",true},
            {"audioDriver",{{"engine","ditto-native-v1"},{"execution","local"},{"mouthReference",mouthReference}}},
            {"segments",avatar::Json::array({{{"path",fs::absolute(audio).string()},{"inputStart",0},{"outputStart",0},{"sampleCount",count},{"gain",1.0}}})}
        });
        ditto::renderTimeline(models,out/"avatar",out/"plan.json",out/"result.json");
        if(pageMode){
            auto result=avatar::readJson(out/"result.json");result["route"]="portrait";result["faces"]=faces;
            {
                // The mouth is driven only by speech. Effects enter the final
                // soundtrack after Ditto has finished the facial motion.
                // Ditto may use an alpha-preserving FFV1 intermediate. Always
                // produce a shareable MP4 for the page, including speech-only.
                VideoReader reader(result.at("path").get<std::string>());const auto info=reader.info();
                const auto final=out/"page.mp4";
                VideoWriter writer(final,mixed,info.width,info.height,true,true,info.fpsNum,info.fpsDen);
                Image frame;while(reader.next(frame)){avatar::checkCancelled();writer.write(frame);}
                writer.finish();result["path"]=final.string();result["effectsMixed"]=!effectPath.empty();
            }
            avatar::writeJson(out/"result.json",result);
        }
        std::cout<<avatar::Json{{"event","complete"},{"result",(out/"result.json").string()}}.dump()<<std::endl;
    }catch(const std::exception &e){std::cerr<<avatar::Json{{"event","error"},{"message",e.what()}}.dump()<<std::endl;return 1;}
}
