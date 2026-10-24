#pragma once
#include <nlohmann/json.hpp>
#include <string>

namespace avatar {
// ORT kernel events establish provider execution, not GPU utilization. In
// particular CoreML CPUAndGPU does not establish per-operation GPU placement.
inline nlohmann::json onnxExecutionReport(const nlohmann::json &events) {
  using J = nlohmann::json;
  J providers = J::object();
  bool cpu = false, gpu = false, unknown = false;
  for (const auto &event : events) {
    if (!event.is_object() || event.value("cat", "") != "Node") continue;
    const auto args = event.value("args", J::object());
    const auto provider = args.value("provider", "");
    if (provider.empty()) continue; // Fence/wait events have no execution evidence.
    auto &entry = providers[provider];
    if (entry.is_null()) entry = {{"kernelEvents", 0}, {"durationUs", 0.0}};
    entry["kernelEvents"] = entry["kernelEvents"].get<size_t>() + 1;
    entry["durationUs"] = entry["durationUs"].get<double>() + event.value("dur", 0.0);
    cpu |= provider == "CPUExecutionProvider";
    gpu |= provider == "DmlExecutionProvider" || provider == "CUDAExecutionProvider" ||
           provider == "WebGpuExecutionProvider";
    unknown |= provider != "CPUExecutionProvider" && provider != "DmlExecutionProvider" &&
               provider != "CUDAExecutionProvider" && provider != "WebGpuExecutionProvider";
  }
  return {{"gpu", gpu ? (cpu ? "mixed" : "yes") : cpu && !unknown ? "no" : "unknown"},
          {"providerExecution", providers}, {"evidence", "onnxruntime-kernel-profile"},
          {"gpuUtilization", "not_measured"}};
}
} // namespace avatar
