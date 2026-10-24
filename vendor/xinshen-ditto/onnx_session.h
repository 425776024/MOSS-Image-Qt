#pragma once
#include "avatar_runtime.h"
#include <onnxruntime_cxx_api.h>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#ifdef MOSS_ONNX_DIRECTML
#include <dml_provider_factory.h>
#include "directml_device.h"
#include "cuda_device.h"
#endif

namespace avatar {
inline Ort::SessionOptions onnxOptions(const std::filesystem::path& model,
                                       const std::string& variant = "",
                                       bool cpuOnly = false,
                                       const std::string& backend = "",
                                       std::filesystem::path* coremlCache = nullptr) {
  Ort::SessionOptions options;
  options.SetIntraOpNumThreads(4);
  options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
#ifdef MOSS_ONNX_DIRECTML
  if (!cpuOnly) {
    const auto providers = Ort::GetAvailableProviders();
    if (backend == "CUDA") {
      if (std::find(providers.begin(), providers.end(), "CUDAExecutionProvider") == providers.end())
        throw std::runtime_error("ONNX Runtime does not include CUDA support");
      const auto adapter = cudaAdapter();
      OrtCUDAProviderOptionsV2 *raw = nullptr;
      const auto &api = Ort::GetApi();
      Ort::ThrowOnError(api.CreateCUDAProviderOptions(&raw));
      const auto release = [&api](OrtCUDAProviderOptionsV2 *value) { api.ReleaseCUDAProviderOptions(value); };
      std::unique_ptr<OrtCUDAProviderOptionsV2, decltype(release)> cuda(raw, release);
      const auto id = std::to_string(adapter.index);
      const char *keys[] = {"device_id", "cudnn_conv_algo_search", "do_copy_in_default_stream", "use_tf32"};
      const char *values[] = {id.c_str(), "HEURISTIC", "1", "0"};
      Ort::ThrowOnError(api.UpdateCUDAProviderOptions(cuda.get(), keys, values, 4));
      Ort::ThrowOnError(api.SessionOptionsAppendExecutionProvider_CUDA_V2(options, cuda.get()));
      std::cerr << Json{{"event","device-selected"},{"component",model.filename().string()},
          {"backend","CUDA"},{"device",adapter.name},{"deviceId",adapter.index},
          {"gpu","unknown"},{"evidence","CUDA-provider-configured; execution not measured"}}.dump() + '\n';
    } else {
    if (std::find(providers.begin(), providers.end(), "DmlExecutionProvider") == providers.end())
      throw std::runtime_error("ONNX Runtime does not include DirectML support");
    options.DisableMemPattern();
    options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
    // ORT partitions unsupported operators to CPU. Failure to create the GPU
    // provider is explicit; do not report GPU acceleration for a CPU session.
    const auto adapter = directMLAdapter();
    // Do not import DML-only exports: the same worker also runs beside CUDA ORT.
    const OrtDmlApi *dml = nullptr;
    Ort::ThrowOnError(Ort::GetApi().GetExecutionProviderApi("DML", ORT_API_VERSION,
        reinterpret_cast<const void**>(&dml)));
    Ort::ThrowOnError(dml->SessionOptionsAppendExecutionProvider_DML(options, adapter.index));
    std::cerr << Json{{"event","device-selected"},{"component",model.filename().string()},
        {"backend","DirectML"},{"device",adapter.name},{"deviceId",adapter.index},
        {"gpu","unknown"},{"evidence","hardware-adapter-selected; execution not measured"}}.dump() + '\n';
    }
  }
#endif
#if defined(__APPLE__) && defined(__aarch64__)
  if (!cpuOnly) {
    const auto providers = Ort::GetAvailableProviders();
    if (backend == "Metal") {
      if (std::find(providers.begin(), providers.end(), "WebGpuExecutionProvider") == providers.end())
        throw std::runtime_error("ONNX Runtime does not include the native Metal/WebGPU provider");
      options.AppendExecutionProvider("WebGPU", {});
      options.AddConfigEntry("session.disable_cpu_ep_fallback", "1");
      return options;
    }
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
    if (coremlCache) *coremlCache = cache;
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
    options.AddConfigEntry("session.disable_cpu_ep_fallback", "1");
    progress("accelerator", 0, 0);
    std::cerr << "[inference] " << model.filename() << " CoreML CPUAndGPU\n";
  }
#endif
  return options;
}
} // namespace avatar
