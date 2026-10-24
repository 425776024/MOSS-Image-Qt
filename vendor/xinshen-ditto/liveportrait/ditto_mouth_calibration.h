#pragma once
#include "ditto_mouth_reference.h"
#include <algorithm>
#include <array>
#include <limits>

namespace ditto::mouth_detail {
inline constexpr const char *automaticRevision = "silent-mouth-calibration-v2";
// The catalog authorizes source calibration; this runtime revision also binds
// emotion-specific references and invalidates pre-expression motion caches.
inline constexpr const char *runtimeRevision = "emotion-linked-mouth-calibration-v3";
inline constexpr int calibrationFrames = 150;
inline constexpr int calibrationSeeds[] = {42, 1024, 2026};

inline std::vector<float> mouthOf(const std::vector<float> &motion) {
  if (motion.size() != 265) throw std::runtime_error("mouth motion dimensions");
  std::vector<float> out;
  for (int p : points) for (int j = 0; j < 3; ++j) out.push_back(motion[202+p*3+j]);
  return out;
}
inline float quantile(std::vector<float> values, double q) {
  if (values.empty()) throw std::runtime_error("empty calibration samples");
  for (float x : values) if (!std::isfinite(x)) throw std::runtime_error("nonfinite calibration");
  std::sort(values.begin(), values.end());
  const double k = q*(values.size()-1);
  const size_t lo = size_t(k), hi = std::min(lo+1, values.size()-1);
  return float(values[lo]+(k-lo)*(values[hi]-values[lo]));
}
// Same estimator as the accepted video: coordinate median over the central
// 100 frames of each of three independent silence predictions (300 samples).
inline J summarizeSilence(const std::vector<std::vector<float>> &runs,
                          const std::vector<float> &sourceMouth) {
  if (runs.size() != 3 || sourceMouth.size() != 18)
    throw std::runtime_error("calibration dimensions");
  for (float v : sourceMouth) if (!std::isfinite(v))
    throw std::runtime_error("nonfinite source mouth");
  std::vector<float> reference(18);
  float spread = 0, temporal = 0, offset = 0;
  for (int c = 0; c < 18; ++c) {
    std::vector<float> all, medians;
    for (const auto &r : runs) {
      if (r.size() != calibrationFrames*265) throw std::runtime_error("silence length");
      std::vector<float> samples;
      for (int f = 25; f < 125; ++f) samples.push_back(r[f*265+202+points[c/3]*3+c%3]);
      medians.push_back(quantile(samples,.5));
      all.insert(all.end(),samples.begin(),samples.end());
    }
    reference[c] = quantile(all,.5);
    spread = std::max(spread, *std::max_element(medians.begin(),medians.end())-
                              *std::min_element(medians.begin(),medians.end()));
    for (auto &v : all) v = std::abs(v-reference[c]);
    temporal = std::max(temporal,quantile(all,.9));
    offset = std::max(offset,std::abs(reference[c]-sourceMouth[c]));
  }
  return {{"referenceMouth",reference}, {"seedSpreadMax",spread},
          {"temporalDeviationQ90Max",temporal}, {"sourceOffsetMax",offset}};
}
// These are rejection bounds, not a quality score or a closed-mouth classifier.
inline std::string geometryRejection(const J &g, double yaw, double pitch) {
  if (!g.is_array() || g.size()!=3) return "source-geometry-unavailable";
  for (const auto &v : g) if (!v.is_number() || !std::isfinite(v.get<double>()))
    return "source-geometry-unreliable";
  const double gap=g[0], thickness=g[1], width=g[2];
  if (!std::isfinite(yaw) || !std::isfinite(pitch) || std::abs(yaw)>35 || std::abs(pitch)>30)
    return "source-pose-outside-validation";
  if (gap<0 || thickness<.03 || thickness>.5 || width<.2 || width>1.2)
    return "source-geometry-unreliable";
  if (gap>.08) return "source-open-mouth-preserved";
  return {};
}
inline std::string silenceStabilityRejection(const J &value) {
  try {
    const auto &r=value.at("referenceMouth");
    if (!nearVector(r,r,18)) return "invalid-silence-reference";
    for (const auto *k : {"seedSpreadMax","temporalDeviationQ90Max","sourceOffsetMax"})
      if (!value.at(k).is_number() || !std::isfinite(value.at(k).get<double>()) || value.at(k).get<double>()<0)
        return "invalid-silence-statistics";
    if (value.at("seedSpreadMax").get<double>()>.003 ||
        value.at("temporalDeviationQ90Max").get<double>()>.006)
      return "unstable-silence-prediction";
    return {};
  } catch (const J::exception &) { return "invalid-silence-reference"; }
}
inline std::string calibrationRejection(const J &value, const std::vector<float> &source) {
  const auto why = silenceStabilityRejection(value);
  if (!why.empty()) return why;
  if (source.size()!=18) return "invalid-silence-reference";
  for (float v : source) if (!std::isfinite(v)) return "invalid-silence-reference";
  const auto &r=value.at("referenceMouth");
  double offset=0;
  for (int i=0;i<18;++i) offset=std::max(offset,std::abs(r[i].get<double>()-source[i]));
  if (offset>.03) return "reference-offset-outside-validation";
  return {};
}
// An emotion can have a stable resting mouth different from the source image.
// Keep the original source/neutral offset bound; separately require the selected
// expression to be stable across seeds and time. Its resting displacement must
// not be misclassified as a source-preparation failure.
inline std::string emotionCalibrationRejection(const J &value, const J &neutral,
                                               const std::vector<float> &source) {
  const auto why = calibrationRejection(neutral, source);
  if (!why.empty()) return "neutral-" + why;
  return silenceStabilityRejection(value);
}
inline double emotionOffsetMax(const J &value, const J &neutral) {
  double offset = 0;
  for (int i=0;i<18;++i)
    offset=std::max(offset,std::abs(value.at("referenceMouth").at(i).get<double>()-
                                  neutral.at("referenceMouth").at(i).get<double>()));
  return offset;
}

// Conservative energy test, not VAD: use only an existing near-silent prefix.
// No samples are inserted, and no gain is applied to detected speech frames.
inline J openingTransition(const std::vector<float> &audio) {
  size_t quiet=0;
  for (size_t i=0; i<std::min<size_t>(audio.size()/640,50); ++i) {
    double energy=0, peak=0;
    for (size_t j=i*640;j<(i+1)*640;++j) {
      const double x=audio[j];
      if (!std::isfinite(x)) throw std::runtime_error("nonfinite opening audio");
      energy+=x*x;peak=std::max(peak,std::abs(x));
    }
    if (std::sqrt(energy/640)>.0001 || peak>.0005) break;
    ++quiet;
  }
  // Leave two unattenuated frames before the first non-silent frame. This also
  // covers breath/weak-onset uncertainty rather than guessing phoneme labels.
  const int end=quiet>=8 ? int(std::min<size_t>(quiet-2,10)) : 0;
  return {{"kind",end ? "existing-silence-smoothstep" : "none"},
          {"quietPrefixFrames",quiet}, {"startFrame",end ? end-6 : 0},
          {"endFrame",end}, {"audioShiftSamples",0}};
}
inline float openingGain(const J &cfg, size_t frame) {
  if (!cfg.contains("mouthOpeningTransition")) return 1;
  const auto &t=cfg.at("mouthOpeningTransition");
  const int end=t.at("endFrame"), start=t.at("startFrame");
  if (end<=0 || frame>=size_t(end)) return 1;
  if (frame<=size_t(start)) return 0;
  const float x=float(frame-start)/float(end-start);
  return x*x*(3-2*x);
}
} // namespace ditto::mouth_detail
