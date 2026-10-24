#pragma once
#include "../avatar_runtime.h"
#include <cmath>
#include <stdexcept>

namespace ditto::control_detail {
using J = avatar::Json;
inline J defaults() {
  return {{"emo", -1}, {"use_d_keys", {{"exp", 1.}, {"pitch", 1.},
          {"yaw", 1.}, {"roll", 1.}, {"t", 1.}}}};
}
// -1 preserves the package's emotion conditioning. A provided dictionary uses
// upstream use_d_keys semantics: omitted motion channels retain the source.
inline J normalize(const J &input) {
  if (!input.is_object()) throw std::runtime_error("人物动作参数必须是对象");
  auto result = defaults();
  if (input.contains("emo")) {
    const auto &value = input.at("emo");
    if (!value.is_number_integer() || value.get<double>() < -1 || value.get<double>() > 7)
      throw std::runtime_error("人物表情编号必须为 -1 至 7");
    result["emo"] = value;
  }
  if (input.contains("use_d_keys")) {
    const auto &weights = input.at("use_d_keys");
    if (!weights.is_object()) throw std::runtime_error("动作幅度必须是分项权重对象");
    auto &target = result["use_d_keys"];
    for (auto &value : target.items()) value.value() = 0.;
    for (const auto &item : weights.items()) {
      if (!target.contains(item.key()) || !item.value().is_number())
        throw std::runtime_error("不支持的动作幅度参数");
      const auto value = item.value().get<double>();
      if (!std::isfinite(value) || value < 0 || value > 2)
        throw std::runtime_error("动作幅度必须在 0 至 2 倍之间");
      target[item.key()] = value;
    }
  }
  return result;
}
inline void apply(J &cfg, const J &controls) {
  cfg["runtimeMotionControls"] = controls;
  const auto emotion = controls.at("emo").get<int>();
  if (emotion < 0) return;
  // Same softmax label conditioning as upstream ConditionHandler._get_emo_avg.
  std::vector<float> values(8, float(1. / (std::exp(8.) + 7.)));
  values[emotion] = float(std::exp(8.) / (std::exp(8.) + 7.));
  cfg["default_kwargs"]["emo"] = J::array({values});
}
inline float gain(const J &cfg, const char *channel) {
  if (!cfg.contains("runtimeMotionControls")) return 1;
  return cfg.at("runtimeMotionControls").at("use_d_keys").at(channel).get<float>();
}
inline bool cacheMatches(const J &cache, const J &controls) {
  return cache.value("motionControls", defaults()) == controls;
}
} // namespace ditto::control_detail
