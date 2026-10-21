#include "OrtRuntime.h"
#include "videocut/ai/model/AiStartupTrace.h"
#include "videocut/base/Sha256.h"
#include <sys/sysctl.h>

#include <coreml_provider_factory.h>
#include <onnxruntime_session_options_config_keys.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fcntl.h>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <sys/file.h>
#include <unistd.h>

namespace videocut::ai::native::detail {
namespace {

Ort::Env &Environment() {
  static Ort::Env environment(ORT_LOGGING_LEVEL_WARNING, "VideoCutAiNativeSdk");
  return environment;
}

bool HasProvider(const std::string &name) {
  const std::vector<std::string> providers = Ort::GetAvailableProviders();
  return std::find(providers.begin(), providers.end(), name) != providers.end();
}

// The model store is immutable and may be replaced when its host app is
// staged. Keep compiled models in the OS-managed per-user cache, independent
// of the model file's location. The verified digest below owns the contents.
#if VIDEOCUT_AI_NATIVE_ALLOW_MODEL_CACHE
std::filesystem::path CoreMlCacheRoot() {
  const auto size = ::confstr(_CS_DARWIN_USER_CACHE_DIR, nullptr, 0);
  if (size == 0) return {};
  std::vector<char> path(size);
  const auto written = ::confstr(_CS_DARWIN_USER_CACHE_DIR, path.data(), path.size());
  if (written == 0 || written > path.size()) return {};
  const std::filesystem::path root(path.data());
  return root.is_absolute() ? root / "com.videocut.ai" / "coreml-cache-v1"
                            : std::filesystem::path{};
}
#endif

// Core ML caches converted models, including weights. Never persist these
// for a decrypted model from a protected pack. The SDK build disables this
// path even when a caller supplies a digest in a distribution build.
class CoreMlCache final {
public:
  CoreMlCache(const std::string &digest, const ComputeBackend backend,
              const OrtSessionWorkload workload, const OrtCoreMlModelFormat format,
              const std::vector<std::pair<std::string, std::int64_t>> &dimensions) {
#if VIDEOCUT_AI_NATIVE_ALLOW_MODEL_CACHE
    if (backend != ComputeBackend::CoreMl || workload != OrtSessionWorkload::FramePipeline ||
        digest.size() != 71 || digest.compare(0, 7, "sha256:") != 0 ||
        digest.find_first_not_of("0123456789abcdef", 7) != std::string::npos) return;
    try {
      const std::string version = OrtGetApiBase()->GetVersionString();
      if (version.empty() || version.find_first_not_of(
              "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.-_") != std::string::npos)
        return;
      std::string shape = "static";
      for (const auto &[name, extent] : dimensions) {
        // These are the only public-model specializations qualified here.
        if ((name != "width" && name != "height") || extent <= 0) return;
        shape += "-" + name + "-" + std::to_string(extent);
      }
      const auto root = CoreMlCacheRoot();
      if (root.empty()) return;
      auto path = root / version /
          digest.substr(7) / (format == OrtCoreMlModelFormat::NeuralNetwork ? "nn-all-fp32" : "mlprogram-all-fp32") / shape;
      std::error_code error;
      std::filesystem::create_directories(path, error);
      if (error) return; // Cache availability never controls model availability.
      const auto lockPath = path / "compile.lock";
      m_lock = ::open(lockPath.c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
      if (m_lock < 0) return;
      if (::flock(m_lock, LOCK_EX) != 0) {
        ::close(m_lock);
        m_lock = -1;
        return;
      }
      m_path = path.string();
    } catch (...) {
      // Cache availability must not turn a usable model into an error.
    }
#else
    (void)digest; (void)backend; (void)workload;
    (void)format; (void)dimensions;
#endif
  }
  ~CoreMlCache() { if (m_lock >= 0) { ::flock(m_lock, LOCK_UN); ::close(m_lock); } }
  CoreMlCache(const CoreMlCache &) = delete;
  CoreMlCache &operator=(const CoreMlCache &) = delete;
  const std::string &Path() const noexcept { return m_path; }
private:
  int m_lock{-1};
  std::string m_path;
};

Ort::SessionOptions MakeOptions(const ComputeBackend backend,
                                const std::uint32_t threads,
                                const OrtSessionWorkload workload,
                                const OrtCoreMlModelFormat coreMlFormat,
                                const std::vector<std::pair<std::string, std::int64_t>> &freeDimensions,
                                const std::string &cacheDirectory) {
  // Provider discovery can log through ORT's process-global default logger.
  // Ensure the SDK environment exists even when an ORT-backed model is the
  // first AI capability used in the process.
  (void)Environment();
  Ort::SessionOptions options;
  options.SetIntraOpNumThreads(static_cast<int>(std::max(1U, threads)));
  options.SetInterOpNumThreads(1);
  options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
  options.DisableMemPattern();
  const char *optimizedGraph = std::getenv("VIDEOCUT_AI_OPTIMIZED_GRAPH_PATH");
  if (optimizedGraph != nullptr && *optimizedGraph != '\0') {
    // Explicit diagnostics only: ORT writes the post-optimization graph with
    // execution-provider assignments, allowing fallback nodes to be named.
    // Never enable this in normal product runs or share one file across
    // concurrent sessions.
    const std::string optimizedGraphPath =
        std::string(optimizedGraph) + "." + std::to_string(::getpid()) + ".ort";
    options.SetOptimizedModelFilePath(optimizedGraphPath.c_str());
  }
  for (const auto &[name, extent] : freeDimensions) {
    if (name.empty() || extent <= 0)
      throw std::invalid_argument("invalid ONNX free dimension override");
    Ort::ThrowOnError(Ort::GetApi().AddFreeDimensionOverrideByName(options, name.c_str(), extent));
  }
  if (workload == OrtSessionWorkload::FramePipeline) {
    // A frame chains multiple independent model sessions, CPU masks and
    // Metal work. Idle workers from the previous Run must not compete with
    // the next stage. ORT 1.23's documented option retains intra-Run spinning
    // but stops it when that session's last concurrent Run returns.
    // This changes scheduling only, not operators, precision or thread count.
    options.AddConfigEntry(kOrtSessionOptionsConfigForceSpinningStop, "1");
  }
  if (backend == ComputeBackend::CoreMl) {
    if (!HasProvider("CoreMLExecutionProvider"))
      throw std::runtime_error("ONNX Runtime Core ML provider is unavailable");
    if (workload == OrtSessionWorkload::FramePipeline) {
      // Core ML chooses GPU/ANE per supported partition. This is accelerator
      // eligibility, not evidence that every operation left the CPU.
      const char *trace = std::getenv("VIDEOCUT_AI_COREML_TRACE");
      const bool profile = trace != nullptr && std::string(trace) == "1";
      std::unordered_map<std::string, std::string> providerOptions{
          {"ModelFormat", coreMlFormat == OrtCoreMlModelFormat::NeuralNetwork
                              ? "NeuralNetwork" : "MLProgram"},
          {"MLComputeUnits", "ALL"},
          {"RequireStaticInputShapes", freeDimensions.empty() ? "0" : "1"},
          {"ProfileComputePlan", profile ? "1" : "0"},
          {"AllowLowPrecisionAccumulationOnGPU", "0"}};
      if (!cacheDirectory.empty()) providerOptions.emplace("ModelCacheDirectory", cacheDirectory);
      if (profile) options.SetLogSeverityLevel(0);
      options.AppendExecutionProvider("CoreML", providerOptions);
    } else {
      const std::uint32_t flags =
          COREML_FLAG_CREATE_MLPROGRAM | COREML_FLAG_USE_CPU_AND_GPU;
      Ort::ThrowOnError(
          OrtSessionOptionsAppendExecutionProvider_CoreML(options, flags));
    }
  } else if (backend == ComputeBackend::Metal) {
    if (!HasProvider("WebGpuExecutionProvider")) {
      throw std::runtime_error(
          "ONNX Runtime WebGPU/Metal provider is unavailable");
    }
    // The locked native ORT package embeds Dawn. On Apple platforms its
    // WebGPU device is backed by Metal. These models have dynamic shapes and
    // may contain CPU fallback nodes, so graph capture is deliberately off.
    // Disable pooled GPU buffers for the first measured qualification pass;
    // this bounds retained memory after the Core ML separation failure.
    const std::unordered_map<std::string, std::string> providerOptions{
        {"preferredLayout", "NCHW"},
        {"enableGraphCapture", "0"},
        {"preserveDevice", "1"},
        // The separation graph has two 62-output Split nodes. Dawn exposes
        // eight
        // storage buffers per shader stage on this device, so keep just
        // these fan-out nodes on CPU and leave the tensor-heavy transformer
        // blocks eligible for Metal.
        {"forceCpuNodeNames", "/band_split/Split\n/mask_estimators.0/Split"},
        {"storageBufferCacheMode", "disabled"},
        {"uniformBufferCacheMode", "disabled"},
        {"queryResolveBufferCacheMode", "disabled"},
        {"defaultBufferCacheMode", "disabled"},
    };
    options.AppendExecutionProvider("WebGPU", providerOptions);
  } else if (backend != ComputeBackend::Cpu) {
    throw std::invalid_argument(
        "ONNX Runtime backend is neither CPU, Core ML, nor Metal");
  }
  return options;
}

std::vector<std::string> ReadNames(Ort::Session &session, const bool inputs) {
  Ort::AllocatorWithDefaultOptions allocator;
  const std::size_t count =
      inputs ? session.GetInputCount() : session.GetOutputCount();
  std::vector<std::string> names;
  names.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    Ort::AllocatedStringPtr value =
        inputs ? session.GetInputNameAllocated(index, allocator)
               : session.GetOutputNameAllocated(index, allocator);
    if (!value || value.get()[0] == '\0')
      throw std::runtime_error("ONNX model contains an empty tensor name");
    names.emplace_back(value.get());
  }
  return names;
}

std::vector<const char *> NamePointers(const std::vector<std::string> &names) {
  std::vector<const char *> pointers;
  pointers.reserve(names.size());
  for (const std::string &name : names)
    pointers.push_back(name.c_str());
  return pointers;
}

} // namespace

bool OrtBackendAvailable(const ComputeBackend backend) noexcept {
  if (backend == ComputeBackend::Cpu) return true;
  try {
    // Check before admitting a lazily shape-specialized detector session.
    // Otherwise missing providers are discovered only on its first Run.
    (void)Environment();
    if (backend == ComputeBackend::CoreMl)
      return HasProvider("CoreMLExecutionProvider");
    if (backend == ComputeBackend::Metal)
      return HasProvider("WebGpuExecutionProvider");
  } catch (...) {
    return false;
  }
  return false;
}

std::string NativeExecutionEnvironmentIdentity() {
  const auto value = [](const char *name) {
    std::size_t length = 0U;
    if (::sysctlbyname(name, nullptr, &length, nullptr, 0U) != 0 ||
        length == 0U || length > 4096U)
      throw std::runtime_error("cannot read native execution environment identity");
    std::string result(length, '\0');
    if (::sysctlbyname(name, result.data(), &length, nullptr, 0U) != 0)
      throw std::runtime_error("native execution environment identity changed");
    result.resize(length);
    if (!result.empty() && result.back() == '\0') result.pop_back();
    return result;
  };
  return value("kern.osversion") + "|" + value("hw.model") + "|" + value("hw.machine");
}

std::string OrtExecutionRecipeIdentity(
    const ComputeBackend backend, const std::uint32_t threads,
    const OrtSessionWorkload workload, const OrtCoreMlModelFormat format,
    const std::vector<std::pair<std::string, std::int64_t>> &dimensions) {
  // The source digest is generated from the actual numerical implementation,
  // so changing fixed options or native post-processing invalidates results.
  std::string material = "videocut.native-vision-execution.v1|";
  const auto field = [&](const std::string &value) {
    material += std::to_string(value.size()) + ":" + value;
  };
  field(VIDEOCUT_NATIVE_VISION_RECIPE_DIGEST);
  field(NativeExecutionEnvironmentIdentity());
  field(OrtGetApiBase()->GetVersionString());
  field(ComputeBackendName(backend));
  field(std::to_string(std::max(1U, threads)));
  field(std::to_string(static_cast<unsigned>(workload)));
  field(std::to_string(static_cast<unsigned>(format)));
  field(std::to_string(dimensions.size()));
  for (const auto &[name, extent] : dimensions) { field(name); field(std::to_string(extent)); }
  const char *trace = std::getenv("VIDEOCUT_AI_COREML_TRACE");
  field(trace != nullptr && std::string(trace) == "1" ? "profile=1" : "profile=0");
  return "sha256:" + base::Sha256::HexDigest(material);
}

OrtModelSession::OrtModelSession(const std::filesystem::path &modelPath,
                                 const ComputeBackend backend,
                                 const std::uint32_t threads,
                                 const OrtSessionWorkload workload,
                                 const OrtCoreMlModelFormat coreMlFormat,
                                 const std::vector<std::pair<std::string, std::int64_t>> &freeDimensions,
                                 const std::string &verifiedModelDigest)
    : m_backend(backend) {
  model::AiStartupTrace sessionTrace("ort_session_create", verifiedModelDigest);
  CoreMlCache cache(verifiedModelDigest, backend, workload, coreMlFormat, freeDimensions);
  m_executionRecipeIdentity = OrtExecutionRecipeIdentity(
      backend, threads, workload, coreMlFormat, freeDimensions);
  Ort::SessionOptions options = MakeOptions(backend, threads, workload, coreMlFormat, freeDimensions, cache.Path());
  try {
    m_session = std::make_unique<Ort::Session>(Environment(), modelPath.c_str(), options);
  } catch (const Ort::Exception &exception) {
    const char *trace = std::getenv("VIDEOCUT_AI_COREML_TRACE");
    if (backend == ComputeBackend::CoreMl && trace != nullptr &&
        std::string(trace) == "1") {
      std::cerr << "[AI_COREML_SESSION] creation_error=" << exception.what() << '\n';
    }
    // The exception does not prove cache corruption. Preserve the selected
    // model/backend failure instead of replaying arbitrary errors uncached.
    throw;
  }
  m_inputNames = ReadNames(*m_session, true);
  m_outputNames = ReadNames(*m_session, false);
  m_inputNamePointers = NamePointers(m_inputNames);
  m_outputNamePointers = NamePointers(m_outputNames);
}

Ort::Session &OrtModelSession::Session() noexcept { return *m_session; }

const std::vector<std::string> &OrtModelSession::InputNames() const noexcept {
  return m_inputNames;
}

const std::vector<std::string> &OrtModelSession::OutputNames() const noexcept {
  return m_outputNames;
}

const std::vector<const char *> &OrtModelSession::InputNamePointers() const noexcept {
  return m_inputNamePointers;
}

const std::vector<const char *> &OrtModelSession::OutputNamePointers() const noexcept {
  return m_outputNamePointers;
}

ComputeBackend OrtModelSession::Backend() const noexcept { return m_backend; }

Ort::MemoryInfo CpuMemoryInfo() {
  return Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
}

std::vector<std::int64_t> TensorShape(const Ort::Value &value) {
  return value.GetTensorTypeAndShapeInfo().GetShape();
}

std::size_t ShapeElementCount(const std::vector<std::int64_t> &shape) {
  std::size_t count = 1;
  for (const std::int64_t dimension : shape) {
    if (dimension < 0)
      throw std::invalid_argument("runtime tensor shape is dynamic");
    const auto value = static_cast<std::size_t>(dimension);
    if (value != 0 && count > std::numeric_limits<std::size_t>::max() / value)
      throw std::overflow_error("runtime tensor element count overflows");
    count *= value;
  }
  return count;
}

} // namespace videocut::ai::native::detail
