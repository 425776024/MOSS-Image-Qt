#include "../liveportrait/ditto_mouth_reference.h"
#include "../liveportrait/ditto_mouth_calibration.h"
#include <iostream>
using J = avatar::Json;
using namespace ditto::mouth_detail;
void check(bool ok) { if (!ok) throw std::runtime_error("mouth policy assertion"); }
int main() {
  const J source = {{"motion", std::vector<float>(265, .2f)},
                    {"kp", std::vector<float>(63, .1f)}};
  const J original = {{"sourceConditioning", "source-neutral-v1"},
                      {"mouthConditionReference", true},
                      {"default_kwargs", {{"ch_info", {{"sentinel", 123}}}}}};
  const J profile = {{"id", "sample"}, {"sourceSha256", "source"},
                     {"sourceMotion", source.at("motion")}, {"sourceKp", source.at("kp")},
                     {"referenceMouth", std::vector<float>(18, .3f)}, {"strength", .5}};
  const J catalog = {{"version", 1}, {"revision", revision}, {"pipelineId", "model"},
                     {"sourceConditioning", "source-neutral-v1"}, {"profiles", J::array({profile})}};
  auto cfg = original;
  auto chosen = select("auto", catalog, "catalog", "source", "model", true, source, cfg);
  check(chosen.at("strategy") == "calibrated" && cfg.at("mouthReferenceExp").size() == 18);
  check(cfg.at("default_kwargs") == original.at("default_kwargs"));
  check(!cacheMatches(J::object(), chosen));
  check(cacheMatches({{"mouthReference", chosen}}, chosen));
  auto manual = select("source", catalog, "catalog", "source", "model", true, source, cfg);
  check(!cfg.contains("mouthReferenceExp") && manual.at("strategy") == "source");
  check(!cacheMatches({{"mouthReference", chosen}}, manual));
  for (const auto &mode : {"unknown", "changed-model", "video", "drift", "duplicate", "invalid", "missing"}) {
    auto c = catalog, s = source;
    std::string hash = "source", model = "model"; bool still = true;
    if (mode == std::string("unknown")) hash = "different-image";
    if (mode == std::string("changed-model")) model = "different-model";
    if (mode == std::string("video")) still = false;
    if (mode == std::string("drift")) s["motion"][202] = .5;
    if (mode == std::string("duplicate")) c["profiles"].push_back(profile);
    if (mode == std::string("invalid")) c["profiles"][0]["referenceMouth"] = J::array({1});
    if (mode == std::string("missing")) c = nullptr;
    const auto fallback = select("auto", c, "catalog", hash, model, still, s, cfg);
    check(fallback.at("strategy") == "source" && !cfg.contains("mouthReferenceExp"));
    check(cacheMatches(J::object(), fallback));
  }
  cfg = original;
  auto changed = select("auto", catalog, "catalog-v2", "source", "model", true, source, cfg);
  check(!cacheMatches({{"mouthReference", chosen}}, changed));
  bool rejected = false;
  try { select("force", catalog, "catalog", "source", "model", true, source, cfg); }
  catch (const std::runtime_error &) { rejected = true; }
  check(rejected);
  // Outlier-resistant neutral estimate, with unstable seeds rejected.
  std::vector<std::vector<float>> runs(3,std::vector<float>(150*265,.01f));
  runs[0][30*265+202+6*3]=.5f;
  const std::vector<float> neutral(18,.01f);
  auto summary=summarizeSilence(runs,neutral);
  check(calibrationRejection(summary,neutral).empty());
  check(nearVector(summary.at("referenceMouth"),neutral,18));
  for (int f=25;f<125;++f) runs[1][f*265+202+6*3]=.02f;
  check(calibrationRejection(summarizeSilence(runs,neutral),neutral)=="unstable-silence-prediction");
  summary["referenceMouth"][0]=.2f;
  check(calibrationRejection(summary,neutral)=="reference-offset-outside-validation");
  summary["seedSpreadMax"]=std::numeric_limits<double>::quiet_NaN();
  check(calibrationRejection(summary,neutral)=="invalid-silence-statistics");
  check(geometryRejection(J::array({.005,.2,.6}),0,0).empty());
  check(geometryRejection(J::array({.18,.2,.6}),0,0)=="source-open-mouth-preserved");
  check(!geometryRejection(J::array({.005,.2,.6}),50,0).empty());
  check(!geometryRejection(J::array({.005,.2,.01}),0,0).empty());
  // An immediate or weak speech onset must never be faded or time-shifted.
  std::vector<float> speech(16000,.01f);
  check(openingTransition(speech).at("endFrame")==0);
  std::fill(speech.begin(),speech.begin()+10*640,0);
  const auto transition=openingTransition(speech);
  check(transition.at("audioShiftSamples")==0 && transition.at("endFrame")==8);
  J fading={{"mouthOpeningTransition",transition}};
  check(openingGain(fading,0)==0 && openingGain(fading,8)==1 && openingGain(fading,10)==1);
  for (int i=1;i<8;++i) check(openingGain(fading,i)>=openingGain(fading,i-1));
  speech[0]=.001f;
  check(openingTransition(speech).at("endFrame")==0);
  std::vector<float> noRoom(640,.0f);
  check(openingTransition(noRoom).at("endFrame")==0);
  std::cout << "mouth reference policy: passed\n";
}
