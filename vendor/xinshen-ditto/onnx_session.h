#pragma once
#include "avatar_runtime.h"
#include <onnxruntime_cxx_api.h>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#ifdef MOSS_ONNX_DIRECTML
#include <dml_provider_factory.h>
#endif

namespace avatar {
inline Ort::SessionOptions onnxOptions(const std::filesystem::path& model,
                                       const std::string& variant = "",
                                       bool cpuOnly = false) {
  Ort::SessionOptions options;
  options.SetIntraOpNumThreads(4);
  options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
#ifdef MOSS_ONNX_DIRECTML
  const char* device = std::getenv("XINSHEN_ONNX_DEVICE");
  if (!cpuOnly && (!device || std::string(device) != "cpu")) {
    options.DisableMemPattern();
    options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
    // ORT partitions unsupported operators to CPU. Failure to create the GPU
    // provider is explicit; do not report GPU acceleration for a CPU session.
    Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_DML(options, 0));
    std::cerr << "[inference] " << model.filename() << " DirectML GPU\n";
  }
#endif
#if defined(__APPLE__) && defined(__aarch64__)
  const char* device = std::getenv("XINSHEN_ONNX_DEVICE");
  if (!cpuOnly && (!device || std::string(device) != "cpu")) {
    const auto providers = Ort::GetAvailableProviders();
    if (std::find(providers.begin(), providers.end(), "CoreMLExecutionProvider") == providers.end())
      throw std::runtime_error("ONNX Runtime 未包含 CoreML GPU 支持");
    const char* configured = std::getenv("XINSHEN_COREML_CACHE");
    auto cache = configured ? std::filesystem::path(configured)
                           : std::filesystem::temp_directory_path() / "xinshen-coreml-cache";
    // ORT's path-based cache key does not detect replaced model bytes.
    cache /= "ort-1.23.2-gpu-v1-" + sha256(model);
    // Fixed dimensions and optimization settings change compiled graphs too.
    if (!variant.empty()) cache /= variant;
    std::filesystem::create_directories(cache);
    std::unordered_map<std::string, std::string> config{
      {"ModelFormat", "MLProgram"}, {"MLComputeUnits", "CPUAndGPU"},
      {"RequireStaticInputShapes", "0"}, {"EnableOnSubgraphs", "0"},
      {"AllowLowPrecisionAccumulationOnGPU", "0"},
      {"ModelCacheDirectory", cache.string()}};
    const char* profile = std::getenv("XINSHEN_INFERENCE_PROFILE");
    if (profile && std::string(profile) != "timing") {
      config["ProfileComputePlan"] = "1";
      options.SetLogSeverityLevel(0);
    }
    options.AppendExecutionProvider("CoreML", config);
    progress("accelerator", 0, 0);
    std::cerr << "[inference] " << model.filename() << " CoreML CPUAndGPU\n";
  }
#endif
  return options;
}
} // namespace avatar
