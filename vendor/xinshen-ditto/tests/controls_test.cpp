#include "../liveportrait/ditto_controls.h"
#include <iostream>
using namespace ditto::control_detail;
void check(bool ok){if(!ok)throw std::runtime_error("motion control assertion");}
int main(){
  const auto base=defaults();
  J cfg={{"default_kwargs",{{"emo",J::array({J::array({.1,.1,.1,.1,.3,.1,.1,.1})})}}}};
  const auto original=cfg.at("default_kwargs");apply(cfg,base);
  check(cfg.at("default_kwargs")==original);
  const auto happy=normalize({{"emo",3},{"use_d_keys",{{"exp",1.3},{"yaw",1.2}}}});
  apply(cfg,happy);const auto emotion=cfg.at("default_kwargs").at("emo").at(0);
  check(emotion.at(3).get<double>()>.99 && emotion.at(4).get<double>()<.001);
  check(gain(cfg,"exp")==1.3f && gain(cfg,"pitch")==0 && gain(cfg,"yaw")==1.2f);
  check(cacheMatches(J::object(),base));check(!cacheMatches(J::object(),happy));
  check(cacheMatches({{"motionControls",happy}},happy));
  auto changed=happy;changed["emo"]=4;check(!cacheMatches({{"motionControls",happy}},changed));
  for(const auto &bad:std::vector<J>{{{"emo",8}},{{"emo",3.2}},{{"use_d_keys",{{"exp",2.1}}}},{{"use_d_keys",{{"jaw",1}}}}}){
    bool rejected=false;try{normalize(bad);}catch(const std::exception&){rejected=true;}check(rejected);
  }
  std::cout<<"emotion conditioning, channel selection, bounds and cache invalidation passed\n";
}
