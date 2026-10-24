#include "../onnx_device_policy.h"
#include "../onnx_execution_report.h"
#include <iostream>

void check(bool value) { if (!value) throw std::runtime_error("ONNX policy assertion"); }
template<class F> void rejects(F f) {
  bool rejected = false;
  try { f(); } catch (const std::runtime_error &) { rejected = true; }
  check(rejected);
}
int main() {
  using namespace avatar;
  using J = nlohmann::json;
  for (const auto *model : {"hubert.onnx", "denoise.onnx", "denoise-condition.onnx",
                            "denoise-step.onnx", "warp.onnx"}) {
    check(windowsGpuRequested(model, false, "", ""));
    check(!windowsGpuRequested(model, false, "cpu", "directml"));
    check(!windowsGpuRequested(model, false, "", "cpu"));
  }
  check(!windowsGpuRequested("stitch.onnx", false, "", ""));
  check(windowsGpuRequested("stitch.onnx", false, "", "DML"));
  check(windowsGpuRequested("decoder.onnx", true, "auto", "auto"));
  check(!windowsGpuRequested("decoder.onnx", true, "directml", "cpu"));
  check(modelDeviceVariable("denoise-step.onnx") == "XINSHEN_ONNX_DEVICE_DENOISE_STEP");
  check(directMLDeviceIndex("") == -1 && directMLDeviceIndex("12") == 12);
  for (const auto *invalid : {"-1", "1junk", "2147483648", " 0"})
    rejects([&] { directMLDeviceIndex(invalid); });
  check(windowsModelDevice("warp.onnx", false, "", "") == "gpu");
  check(windowsModelDevice("warp.onnx", false, "cuda", "") == "cuda");
  check(windowsModelDevice("warp.onnx", false, "auto", "cuda") == "cuda");
  check(windowsModelDevice("warp.onnx", true, "cpu", "cuda") == "cpu");
  requireAppleAcceleration("auto", true);
  requireAppleAcceleration("metal", true);
  rejects([] { requireAppleAcceleration("cpu", true); });
  rejects([] { requireAppleAcceleration("CPU", true); });
  rejects([] { requireAppleAcceleration("auto", false); });
  rejects([] { windowsGpuRequested("warp.onnx", false, "", "typo"); });
  auto events = J::array({
      {{"cat", "Node"}, {"dur", 3}, {"args", {{"provider", "CPUExecutionProvider"}}}},
      {{"cat", "Node"}, {"dur", 7}, {"args", {{"provider", "DmlExecutionProvider"}}}},
      {{"cat", "Node"}, {"dur", 5}, {"args", {{"provider", "DmlExecutionProvider"}}}},
      {{"cat", "Node"}, {"name", "fence"}, {"dur", 1000}},
      {{"cat", "Session"}, {"args", {{"provider", "CUDAExecutionProvider"}}}}});
  auto mixed = onnxExecutionReport(events);
  check(mixed.at("gpu") == "mixed");
  check(mixed.at("providerExecution").at("DmlExecutionProvider").at("kernelEvents") == 2);
  check(mixed.at("providerExecution").at("DmlExecutionProvider").at("durationUs") == 12);
  check(mixed.at("gpuUtilization") == "not_measured");
  events.erase(events.begin());
  check(onnxExecutionReport(events).at("gpu") == "yes");
  check(onnxExecutionReport(J::array()).at("gpu") == "unknown");
  check(onnxExecutionReport(J::array({{{"cat", "Node"}, {"dur", 10}, {"args", {{"provider", "CUDAExecutionProvider"}}}}})).at("gpu") == "yes");
  check(onnxExecutionReport(J::array({{{"cat", "Node"}, {"dur", 10}, {"args", {{"provider", "WebGpuExecutionProvider"}}}}})).at("gpu") == "yes");
  check(onnxExecutionReport(J::array({{{"cat", "Node"}, {"args", {{"provider", "CoreMLExecutionProvider"}}}}})).at("gpu") == "unknown");
  std::cout << "Windows model routing, overrides, adapter index and execution evidence passed\n";
}
