#pragma once
#include "../avatar_runtime.h"
#include <cmath>
#include <string>
#include <vector>

namespace ditto::mouth_detail {
using J = avatar::Json;
inline constexpr const char *revision = "validated-source-reference-v1";
inline constexpr int points[] = {6, 12, 14, 17, 19, 20};

inline bool nearVector(const J &a, const J &b, size_t size) {
  if (!a.is_array() || !b.is_array() || a.size() != size || b.size() != size)
    return false;
  for (size_t i = 0; i < size; ++i) {
    if (!a[i].is_number() || !b[i].is_number()) return false;
    const double x = a[i].get<double>(), y = b[i].get<double>();
    if (!std::isfinite(x) || !std::isfinite(y) || std::abs(x - y) > 1e-5)
      return false;
  }
  return true;
}

inline J neutralEmotionCondition() {
  std::vector<float> emotion(8, float(1. / (std::exp(8.) + 7.)));
  emotion[4] = float(std::exp(8.) / (std::exp(8.) + 7.));
  return J::array({emotion});
}
inline bool isNeutralEmotion(const J &emotion) {
  if (!emotion.is_array() || emotion.empty()) return false;
  const auto neutral = neutralEmotionCondition().at(0);
  for (const auto &frame : emotion)
    if (!nearVector(frame, neutral, 8)) return false;
  return true;
}

// A calibrated source is an exact, reviewed image/model pair, not a general
// mouth-shape classifier. Unknown images and uncertain preparation keep the
// source reference. In particular, an open/closed-mouth threshold alone failed
// cross-person validation and must not authorize a profile.
inline J select(const std::string &mode, const J &catalog,
                const std::string &catalogHash, const std::string &sourceHash,
                const std::string &pipelineId, bool still, const J &source,
                J &cfg) {
  if (mode != "auto" && mode != "source")
    throw std::runtime_error("Ditto: 不支持的口型适配模式");
  cfg.erase("mouthReferenceExp");
  J decision = {{"revision", revision}, {"mode", mode},
                {"strategy", "source"}, {"profileId", ""},
                {"strength", 0}, {"catalogSha256", catalogHash},
                {"sourceSha256", sourceHash}, {"reason", "unreviewed-source"}};
  if (mode == "source") { decision["reason"] = "manual-source"; return decision; }
  if (!still) { decision["reason"] = "video-source"; return decision; }
  // Reviewed catalog profiles were measured with source-neutral conditioning.
  // An explicit expression needs its own silence reference, including for
  // portraits with a reviewed partial correction.
  if (cfg.value("sourceConditioning", "") == "source-neutral-v1" &&
      !isNeutralEmotion(cfg.at("default_kwargs").value("emo", neutralEmotionCondition()))) {
    decision["reason"] = "emotion-reference-required"; return decision;
  }
  try {
    if (catalog.is_null()) { decision["reason"] = "catalog-unavailable"; return decision; }
    if (catalog.at("version") != 1 || catalog.at("revision") != revision ||
        catalog.at("pipelineId") != pipelineId ||
        catalog.at("sourceConditioning") != cfg.value("sourceConditioning", "") ||
        !cfg.value("mouthConditionReference", false)) {
      decision["reason"] = "incompatible-contract"; return decision;
    }
    const J *match = nullptr;
    for (const auto &profile : catalog.at("profiles")) {
      if (profile.at("sourceSha256") != sourceHash) continue;
      if (match) { decision["reason"] = "ambiguous-profile"; return decision; }
      match = &profile;
    }
    if (!match) return decision;
    const auto &profile = *match;
    if (!nearVector(profile.at("sourceMotion"), source.at("motion"), 265) ||
        !nearVector(profile.at("sourceKp"), source.at("kp"), 63)) {
      decision["reason"] = "source-preparation-drift"; return decision;
    }
    const auto &exp = profile.at("referenceMouth");
    const double strength = profile.at("strength").get<double>();
    if (!nearVector(exp, exp, 18) || !std::isfinite(strength) ||
        strength <= 0 || strength > 1 || profile.at("id").get<std::string>().empty()) {
      decision["reason"] = "invalid-profile"; return decision;
    }
    // This field is read only by stitching. ch_info remains the prediction
    // condition, so speech rhythm, head motion and seed stay unchanged.
    cfg["mouthReferenceExp"] = exp;
    decision["strategy"] = "calibrated";
    decision["profileId"] = profile.at("id");
    decision["strength"] = strength;
    decision["reason"] = "reviewed-source-model-pair";
  } catch (const J::exception &) {
    decision["reason"] = "invalid-catalog";
  }
  return decision;
}

inline bool cacheMatches(const J &cache, const J &decision) {
  // Legacy source-reference caches remain usable only when no correction is
  // selected. A calibrated cache cannot be silently reused after a mode,
  // profile, source or policy change.
  return cache.contains("mouthReference") ? cache.at("mouthReference") == decision
                                         : decision.at("strategy") == "source";
}
} // namespace ditto::mouth_detail
