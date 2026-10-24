// Native implementation of antgroup/ditto-talkinghead c3e47eee inference rules.
// Upstream Apache-2.0. No Python, network, DPT, or FLAME inference at runtime.
#include "ditto.h"
#include "ditto_segments.h"
#include "ditto_source_condition.h"
#include "ditto_mouth_reference.h"
#include "ditto_mouth_calibration.h"
#include "ditto_controls.h"
#include "mouth_cache_store.h"
#if defined(__APPLE__)
#include "coreml.h"
#include <pthread.h>
#include <pthread/qos.h>
#endif
#include "../frame_pipeline.h"
#include "../media_io.h"
#include "../onnx_session.h"
#include <chrono>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <new>
#include <numeric>
#include <opencv2/dnn.hpp>
#include <opencv2/imgproc.hpp>
#include "yunet_alignment.h"
#include <random>
#include <thread>
namespace ditto {
namespace fs = std::filesystem;
using J = avatar::Json;
using V = std::vector<float>;
namespace {
using Clock = std::chrono::steady_clock;
double seconds(Clock::time_point start) {
  return std::chrono::duration<double>(Clock::now() - start).count();
}
struct AccumulateTime {
  double &total;
  Clock::time_point start = Clock::now();
  ~AccumulateTime() { total += seconds(start); }
};
void require(bool ok, const std::string &msg) {
  if (!ok)
    throw std::runtime_error("Ditto: " + msg);
}
bool inferencePriorityEnabled() noexcept {
#if defined(__APPLE__)
  const char *value = std::getenv("XINSHEN_DITTO_QOS_USER_INITIATED");
  return !value || std::strcmp(value, "1") == 0;
#else
  return false;
#endif
}
#if defined(__APPLE__)
struct PriorityCounters {
  std::atomic<size_t> overrideStarts{0}, overrideStartFailures{0}, overrideEnds{0}, overrideEndFailures{0};
  std::atomic<size_t> ortWorkersCreated{0}, ortWorkerCreateFailures{0}, ortWorkerSetFailures{0}, ortWorkersJoined{0};
} priorityCounters;
#endif
// A temporary override leaves the caller's requested QoS unchanged, including
// UNSPECIFIED (which cannot be restored with pthread_set_qos_class_self_np).
// Every scope ends on its owning thread, including exception/cancellation exits.
class InferencePriority {
 public:
  explicit InferencePriority(const char *role, bool report = true) noexcept
#if defined(__APPLE__)
      : role_(role), report_(report)
#endif
  {
#if defined(__APPLE__)
    if (!inferencePriorityEnabled()) return;
    qos_class_t requested = QOS_CLASS_UNSPECIFIED;
    int relative = 0;
    const int queryStatus = pthread_get_qos_class_np(pthread_self(), &requested, &relative);
    errno = 0;
    token_ = pthread_override_qos_class_start_np(pthread_self(), QOS_CLASS_USER_INITIATED, 0);
    const int startError = token_ ? 0 : errno;
    ++priorityCounters.overrideStarts;
    if (!token_) ++priorityCounters.overrideStartFailures;
    if (report_)
      std::fprintf(stderr, "[ditto_qos] role=%s requested=%u relative=%d query_status=%d override=%d error=%d\n",
                   role_, unsigned(requested), relative, queryStatus, token_ != nullptr, startError);
#else
    (void)role;
    (void)report;
#endif
  }
  ~InferencePriority() noexcept {
#if defined(__APPLE__)
    if (!token_) return;
    const int status = pthread_override_qos_class_end_np(token_);
    ++priorityCounters.overrideEnds;
    if (status) ++priorityCounters.overrideEndFailures;
    if (report_)
      std::fprintf(stderr, "[ditto_qos] role=%s override_end_status=%d\n", role_, status);
#endif
  }
  InferencePriority(const InferencePriority &) = delete;
  InferencePriority &operator=(const InferencePriority &) = delete;
 private:
#if defined(__APPLE__)
  const char *role_;
  bool report_;
  pthread_override_t token_ = nullptr;
#endif
};
J inferencePriorityStats() {
  J result = {{"enabled", inferencePriorityEnabled()}};
#if defined(__APPLE__)
  result.update({{"overrideStarts", priorityCounters.overrideStarts.load()},
                 {"overrideStartFailures", priorityCounters.overrideStartFailures.load()},
                 {"overrideEnds", priorityCounters.overrideEnds.load()},
                 {"overrideEndFailures", priorityCounters.overrideEndFailures.load()},
                 {"ortWorkersCreated", priorityCounters.ortWorkersCreated.load()},
                 {"ortWorkerCreateFailures", priorityCounters.ortWorkerCreateFailures.load()},
                 {"ortWorkerSetFailures", priorityCounters.ortWorkerSetFailures.load()},
                 {"ortWorkersJoined", priorityCounters.ortWorkersJoined.load()}});
#endif
  return result;
}
#if defined(__APPLE__)
// The returned opaque handle owns this allocation. ORT's join callback is its
// sole deleter, after pthread_join; the worker only borrows it. No Net, model,
// session-options, or stack pointer is captured beyond ORT's own worker context.
struct PriorityOrtThread {
  pthread_t thread;
  OrtThreadWorkerFn work;
  void *parameter;
};
void *runPriorityOrtThread(void *opaque) noexcept {
  auto *worker = static_cast<PriorityOrtThread *>(opaque);
  qos_class_t before = QOS_CLASS_UNSPECIFIED, after = QOS_CLASS_UNSPECIFIED;
  int relative = 0;
  const int beforeStatus = pthread_get_qos_class_np(pthread_self(), &before, &relative);
  const int setStatus = pthread_set_qos_class_self_np(QOS_CLASS_USER_INITIATED, 0);
  const int afterStatus = pthread_get_qos_class_np(pthread_self(), &after, &relative);
  if (setStatus) ++priorityCounters.ortWorkerSetFailures;
  std::fprintf(stderr, "[ditto_qos] role=ort-worker requested_before=%u requested_after=%u query_before=%d query_after=%d set_status=%d\n",
               unsigned(before), unsigned(after), beforeStatus, afterStatus, setStatus);
  try {
    worker->work(worker->parameter);
  } catch (...) {
    // An ORT worker may not throw across a C/pthread boundary. Continuing after
    // an escaped worker exception would leave its thread pool waiting forever.
    std::fprintf(stderr, "[ditto_qos] ORT worker escaped with an exception\n");
    std::terminate();
  }
  return nullptr;
}
OrtCustomThreadHandle createPriorityOrtThread(void *, OrtThreadWorkerFn work,
                                             void *parameter) noexcept {
  if (!work) return nullptr;
  auto *worker = new (std::nothrow) PriorityOrtThread{{}, work, parameter};
  if (!worker) {
    ++priorityCounters.ortWorkerCreateFailures;
    return nullptr;
  }
  const int status = pthread_create(&worker->thread, nullptr, runPriorityOrtThread, worker);
  if (status) {
    delete worker;
    ++priorityCounters.ortWorkerCreateFailures;
    std::fprintf(stderr, "[ditto_qos] ORT worker creation status=%d\n", status);
    return nullptr; // ORT unwinds and joins workers already created for this pool.
  }
  ++priorityCounters.ortWorkersCreated;
  return reinterpret_cast<OrtCustomThreadHandle>(worker);
}
void joinPriorityOrtThread(OrtCustomThreadHandle handle) noexcept {
  if (!handle) return;
  auto *worker = reinterpret_cast<PriorityOrtThread *>(const_cast<OrtCustomHandleType *>(handle));
  const int status = pthread_join(worker->thread, nullptr);
  if (status) {
    // A pool joins each handle exactly once from a different thread. A failed
    // join violates that ownership contract; deleting live context is unsafe.
    std::fprintf(stderr, "[ditto_qos] ORT worker join status=%d\n", status);
    std::terminate();
  }
  ++priorityCounters.ortWorkersJoined;
  delete worker;
}
#endif
Ort::Env &runtimeEnvironment() {
  // ORT 1.23 initializes its API pointer dynamically. Calling it from another
  // global initializer can dereference a null pointer on MSVC before main().
  static Ort::Env environment(ORT_LOGGING_LEVEL_WARNING, "xinshen-ditto");
  return environment;
}
Ort::MemoryInfo &runtimeMemory() {
  (void)runtimeEnvironment();
  static auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
  return memory;
}
V flatten(const J &j) {
  V v;
  if (j.is_array())
    for (const auto &x : j) {
      auto a = flatten(x);
      v.insert(v.end(), a.begin(), a.end());
    }
  else
    v.push_back(j.get<float>());
  return v;
}
void finite(const V &x) {
  for (float v : x)
    if (!std::isfinite(v))
      require(false, "nonfinite tensor");
}
void save(const fs::path &p, const V &v) {
  std::ofstream f(p, std::ios::binary);
  f.write(reinterpret_cast<const char *>(v.data()), v.size() * 4);
  require(bool(f), "write tensor");
}
V read(const fs::path &p, size_t n) {
  require(fs::file_size(p) == n * 4, "tensor byte count " + p.string());
  V v(n);
  std::ifstream f(p, std::ios::binary);
  f.read(reinterpret_cast<char *>(v.data()), n * 4);
  require(bool(f), "read tensor");
  finite(v);
  return v;
}
void dump(const std::string &name, const V &v) {
  if (const char *d = std::getenv("XINSHEN_DITTO_TRACE")) {
    fs::create_directories(d);
    save(fs::path(d) / (name + ".f32"), v);
  }
}
struct Net {
  Ort::Session session{nullptr};
#if defined(__APPLE__)
  std::unique_ptr<lp::CoreMLGenerator> native;
#endif
  std::vector<std::string> inputs, outputs;
  std::string name;
  double loadSeconds = 0, runSeconds = 0;
  size_t runs = 0;
  ~Net() {
    if (std::getenv("XINSHEN_INFERENCE_PROFILE"))
      std::cerr << "[ditto_net] " << name << " load_s=" << loadSeconds
                << " runs=" << runs << " run_s=" << runSeconds << '\n';
  }
  std::unique_ptr<Net> publicWarp, publicDecoder;
  Net(const fs::path &p, bool gpu = true, int cooperativeWorkers = 0)
      : name(p.filename().string()) {
    AccumulateTime timer{loadSeconds};
    if(name=="generator.onnx"&&!fs::exists(p)&&fs::exists(p.parent_path()/"warp.onnx")) {
      // Official pretrained weights, split ONNX layout. The opset-20 warp
      // is a public format conversion with unchanged weights, not a finetune.
      publicWarp=std::make_unique<Net>(p.parent_path()/"warp.onnx",false);
      publicDecoder=std::make_unique<Net>(p.parent_path()/"decoder.onnx",true);
      require(publicWarp->inputs==std::vector<std::string>{"feature_3d","kp_source","kp_driving"}&&
              publicDecoder->inputs==std::vector<std::string>{"feature"}, "public renderer tensor names");
      inputs={"feature_3d","kp_driving","kp_source"};return;
    }
#if defined(__APPLE__)
    if (gpu && name == "generator.onnx") {
      native = lp::CoreMLGenerator::create(p);
      if (native) {
        inputs = {"feature_3d", "kp_driving", "kp_source"};
        return;
      }
    }
#endif
    auto selected = p;
    const char *device = std::getenv("XINSHEN_ONNX_DEVICE");
    if (gpu && (!device || std::string(device) != "cpu") &&
        (name == "generator.onnx" || name == "appearance.onnx")) {
      auto manifestPath = p.parent_path() / "coreml/manifest.json";
      if (fs::exists(manifestPath)) {
        auto manifest = avatar::readJson(manifestPath);
        require(manifest.at("version") == 1 &&
                    manifest.at("loweringVersion") == 5,
                "GPU graph version");
        bool found = false;
        for (auto &entry : manifest.at("files")) {
          if (entry.at("file") != name)
            continue;
          selected = p.parent_path() / "coreml" / name;
          require(entry.at("sourceSha256") == avatar::sha256(p) &&
                      entry.at("sha256") == avatar::sha256(selected),
                  "GPU graph checksum");
          found = true;
        }
        require(found, "GPU graph missing");
      }
    }
    auto &environment = runtimeEnvironment();
    auto options = avatar::onnxOptions(selected, "ditto-v1", !gpu);
    // These large CPU models benefit from more workers; small landmark and
    // stitching models retain the shared runtime's four-thread policy.
    if (!gpu && (name == "hubert.onnx" || name == "denoise.onnx" ||
                 name == "denoise-condition.onnx" || name == "denoise-step.onnx"))
      options.SetIntraOpNumThreads(
          std::max(1u, std::min(8u, std::thread::hardware_concurrency())));
    if (!gpu && cooperativeWorkers > 0) {
      options.SetIntraOpNumThreads(cooperativeWorkers);
      // Upstream work has spare throughput. Sleeping workers leave CPU and
      // memory bandwidth available to the concurrent GPU/ANE render stages.
      options.AddConfigEntry("session.intra_op.allow_spinning", "0");
      options.AddConfigEntry("session.inter_op.allow_spinning", "0");
    }
#if defined(__APPLE__)
    if (!gpu && inferencePriorityEnabled()) {
      options.SetCustomCreateThreadFn(createPriorityOrtThread);
      options.SetCustomThreadCreationOptions(nullptr);
      options.SetCustomJoinThreadFn(joinPriorityOrtThread);
    }
#endif
    session = Ort::Session(environment, selected.c_str(), options);
    Ort::AllocatorWithDefaultOptions a;
    for (size_t i = 0; i < session.GetInputCount(); ++i)
      inputs.emplace_back(session.GetInputNameAllocated(i, a).get());
    for (size_t i = 0; i < session.GetOutputCount(); ++i)
      outputs.emplace_back(session.GetOutputNameAllocated(i, a).get());
  }
  std::vector<V> run(const std::vector<V> &data,
                     const std::vector<std::vector<int64_t>> &shapes,
                     int64_t step = -1) {
    if(publicWarp) {
      AccumulateTime timer{runSeconds};++runs;
      require(data.size()==3&&shapes.size()==3,"public renderer input count");
      auto warped=publicWarp->runRefs({&data[0],&data[2],&data[1]},{shapes[0],shapes[2],shapes[1]});
      require(warped.size()==1&&warped[0].size()==256*64*64,"public warp output shape");
      return publicDecoder->runRefs({&warped[0]},{{1,256,64,64}});
    }
#if defined(__APPLE__)
    if (native) {
      AccumulateTime timer{runSeconds};
      ++runs;
      avatar::checkCancelled();
      auto result = native->run(inputs, data, shapes);
      finite(result);
      std::vector<V> values;
      values.emplace_back(std::move(result));
      return values;
    }
#endif
    std::vector<const V *> refs;
    for (const auto &value : data) refs.push_back(&value);
    return runRefs(refs, shapes, step);
  }
  std::vector<V> runRefs(const std::vector<const V *> &data,
                        const std::vector<std::vector<int64_t>> &shapes,
                        int64_t step = -1) {
    AccumulateTime timer{runSeconds};
    ++runs;
    avatar::checkCancelled();
    require(data.size() == inputs.size() && shapes.size() == inputs.size(), "input count " + name);
    std::vector<Ort::Value> xs;
    std::vector<const char *> ins, outs;
    for (size_t i = 0; i < data.size(); ++i) {
      size_t n = 1;
      for (auto x : shapes[i]) {
        require(x > 0, "shape");
        n *= x;
      }
      require(data[i] && n == data[i]->size(), "shape " + name);
      ins.push_back(inputs[i].c_str());
      if (inputs[i] == "time_cond")
        xs.push_back(Ort::Value::CreateTensor<int64_t>(
            runtimeMemory(), &step, 1, shapes[i].data(), shapes[i].size()));
      else
        xs.push_back(Ort::Value::CreateTensor<float>(
            runtimeMemory(), const_cast<float *>(data[i]->data()), n, shapes[i].data(),
            shapes[i].size()));
    }
    for (auto &s : outputs)
      outs.push_back(s.c_str());
    auto ys = session.Run(Ort::RunOptions{nullptr}, ins.data(), xs.data(),
                          xs.size(), outs.data(), outs.size());
    std::vector<V> result;
    for (auto &y : ys) {
      auto n = y.GetTensorTypeAndShapeInfo().GetElementCount();
      auto *p = y.GetTensorData<float>();
      result.emplace_back(p, p + n);
      finite(result.back());
    }
    return result;
  }
};
struct ConditionPackage {
  fs::path directory;
  J manifest;
  std::string sha256;
};
ConditionPackage conditionPackage(const fs::path &root) {
  const char *candidate = std::getenv("XINSHEN_DITTO_CONDITION_CANDIDATE");
  const bool explicitCandidate = candidate && *candidate;
  const auto directory = explicitCandidate ? fs::path(candidate) : root / "condition-cache";
  const auto path = directory / "manifest.json";
  if (!fs::exists(path)) {
    require(!explicitCandidate, "condition candidate manifest missing");
    return {};
  }
  auto manifest = avatar::readJson(path);
  require(manifest.at("version") == 1 &&
              manifest.at("experiment") == "window-invariant-denoiser-conditioning" &&
              manifest.at("sourceSha256") == avatar::sha256(root / "denoise.onnx"),
          "condition candidate source/version");
  for (const auto *part : {"condition", "step"}) {
    const std::string expected = std::string("denoise-") + part + ".onnx";
    const auto &entry = manifest.at(part);
    const auto file = directory / expected;
    require(entry.at("file") == expected && !fs::is_symlink(file) &&
                fs::is_regular_file(file) && fs::file_size(file) == entry.at("bytes").get<uintmax_t>() &&
                avatar::sha256(file) == entry.at("sha256"), "condition candidate checksum");
  }
  return {directory, std::move(manifest), avatar::sha256(path)};
}
struct DenoiseStats {
  std::string manifestSha256;
  double conditioningSeconds = 0, stepSeconds = 0;
  size_t windows = 0, steps = 0;
  J json() const {
    return {{"conditionManifestSha256", manifestSha256},
            {"conditioningSeconds", conditioningSeconds}, {"stepSeconds", stepSeconds},
            {"windows", windows}, {"steps", steps}};
  }
};
class Denoiser {
 public:
  Denoiser(const fs::path &root, int workers, DenoiseStats &stats) : stats_(stats) {
    auto package = conditionPackage(root);
    stats_.manifestSha256 = package.sha256;
    if (package.directory.empty()) {
      canonical_ = std::make_unique<Net>(root / "denoise.onnx", false, workers);
      return;
    }
    condition_ = std::make_unique<Net>(package.directory / "denoise-condition.onnx", false, workers);
    step_ = std::make_unique<Net>(package.directory / "denoise-step.onnx", false, workers);
    const std::vector<std::string> cachedNames = {
        "/Where_output_0", "/Where_1_output_0", "/Where_2_output_0", "/Where_3_output_0"};
    cacheShapes_ = {{1, 80, 512}, {1, 512}, {1, 80, 512}, {1, 512}};
    stepShapes_ = {{1, 80, 265}, {1, 265}, {1}};
    stepShapes_.insert(stepShapes_.end(), cacheShapes_.begin(), cacheShapes_.end());
    auto stepNames = std::vector<std::string>{"x", "cond_frame", "time_cond"};
    stepNames.insert(stepNames.end(), cachedNames.begin(), cachedNames.end());
    require(condition_->inputs == std::vector<std::string>{"cond"} && condition_->outputs == cachedNames &&
                step_->inputs == stepNames && step_->outputs == std::vector<std::string>{"pred_noise", "x_start"},
            "condition candidate tensor names");
    validate(*condition_, package.manifest.at("condition"), {{1, 80, 1103}}, cacheShapes_);
    validate(*step_, package.manifest.at("step"), stepShapes_, {{1, 80, 265}, {1, 80, 265}});
    std::cerr << "[ditto_condition_cache] manifest=" << stats_.manifestSha256
              << " bytes_per_window=" << (2 * 80 * 512 + 2 * 512) * sizeof(float) << '\n';
  }
  void prepare(const V &condition) {
    ++stats_.windows;
    if (!condition_) return;
    AccumulateTime timer{stats_.conditioningSeconds};
    cached_ = condition_->runRefs({&condition}, {{1, 80, 1103}});
    require(cached_.size() == cacheShapes_.size(), "condition cache output count");
    for (size_t i = 0; i < cached_.size(); ++i) {
      size_t count = 1;
      for (const auto dimension : cacheShapes_[i]) count *= dimension;
      require(cached_[i].size() == count, "condition cache output size");
    }
  }
  std::vector<V> run(const V &x, const V &kp, const V &condition, int64_t time) {
    AccumulateTime timer{stats_.stepSeconds};
    ++stats_.steps;
    if (canonical_)
      return canonical_->runRefs({&x, &kp, &condition, &timePlaceholder_},
                                {{1, 80, 265}, {1, 265}, {1, 80, 1103}, {1}}, time);
    require(cached_.size() == cacheShapes_.size(), "condition cache not prepared");
    std::vector<const V *> values{&x, &kp, &timePlaceholder_};
    for (const auto &value : cached_) values.push_back(&value);
    return step_->runRefs(values, stepShapes_, time);
  }
 private:
  static void validate(Net &net, const J &entry,
                       const std::vector<std::vector<int64_t>> &inputs,
                       const std::vector<std::vector<int64_t>> &outputs) {
    for (bool input : {true, false}) {
      const auto &names = input ? net.inputs : net.outputs;
      const auto &shapes = input ? inputs : outputs;
      const auto &declared = entry.at(input ? "inputs" : "outputs");
      require(declared.is_array() && declared.size() == names.size() && shapes.size() == names.size(),
              "condition candidate manifest tensor count");
      for (size_t i = 0; i < names.size(); ++i) {
        const auto type = input ? net.session.GetInputTypeInfo(i) : net.session.GetOutputTypeInfo(i);
        const auto tensor = type.GetTensorTypeAndShapeInfo();
        const bool integer = input && names[i] == "time_cond";
        require(declared[i].at("name") == names[i] &&
                    declared[i].at("shape").get<std::vector<int64_t>>() == shapes[i] &&
                    declared[i].at("dtype") == (integer ? "INT64" : "FLOAT") &&
                    tensor.GetShape() == shapes[i] && tensor.GetElementType() ==
                        (integer ? ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64 : ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT),
                "condition candidate tensor shape/type");
      }
    }
  }
  DenoiseStats &stats_;
  std::unique_ptr<Net> canonical_, condition_, step_;
  std::vector<V> cached_;
  std::vector<std::vector<int64_t>> cacheShapes_, stepShapes_;
  V timePlaceholder_{0.f};
};
J verify(const fs::path &root) {
  auto m = avatar::readJson(root / "manifest.json");
  require(m.at("version") == 1 && m.at("engine") == "ditto-native-v1",
          "model manifest");
  for (auto it = m.at("files").begin(); it != m.at("files").end(); ++it) {
    fs::path rel = it.key();
    require(rel.filename() == rel && !rel.is_absolute(), "model path");
    auto p = root / rel;
    require(fs::is_regular_file(p) &&
                fs::file_size(p) == it.value().at("bytes").get<uintmax_t>() &&
                avatar::sha256(p) == it.value().at("sha256"),
            "model checksum " + rel.string());
  }
  return m;
}
cv::Mat rgb(const Image &image) {
  cv::Mat out;
  cv::cvtColor(cv::Mat(image.height, image.width, CV_8UC3,
                       const_cast<uint8_t *>(image.bgr.data())),
               out, cv::COLOR_BGR2RGB);
  return out;
}
V tensor(const cv::Mat &image, float scale = 1.f / 255, bool swap = false,
         float mean = 0) {
  require(image.type() == CV_8UC3, "source tensor requires RGB8");
  // Avoid a four-dimensional cv::Mat crossing OpenCV/MediaPipe shared-library
  // allocator boundaries. Both libraries export cv::Mat destruction symbols.
  // Conversion is the same planar (pixel - mean) * scale, with no image resize.
  const size_t plane = image.total();
  V output(plane * 3);
  for (int y = 0; y < image.rows; ++y) {
    const auto *row = image.ptr<cv::Vec3b>(y);
    for (int x = 0; x < image.cols; ++x)
      for (int c = 0; c < 3; ++c)
        output[c * plane + y * image.cols + x] =
            (float(row[x][swap ? 2 - c : c]) - mean) * scale;
  }
  return output;
}
cv::Mat cropMatrix(const std::vector<cv::Point2f> &p, int size, float expansion,
                   float shift) {
  auto avg = [&](std::initializer_list<int> ids) {
    cv::Point2f v{};
    for (int i : ids)
      v += p.at(i);
    return v / float(ids.size());
  };
  cv::Point2f eyes, mouth;
  if (p.size() == 106) {
    eyes = (avg({33, 35, 40, 39}) + avg({87, 89, 94, 93})) / 2;
    mouth = avg({52, 61});
  } else {
    require(p.size() == 203, "landmarks");
    eyes = (avg({0, 6, 12, 18}) + avg({24, 30, 36, 42})) / 2;
    mouth = avg({48, 66});
  }
  auto uy = mouth - eyes;
  float len = cv::norm(uy);
  uy = len <= .001f ? cv::Point2f(0, 1) : uy / len;
  cv::Point2f ux(uy.y, -uy.x), c{};
  for (auto v : p)
    c += v;
  c /= float(p.size());
  cv::Point2f lo(1e9, 1e9), hi(-1e9, -1e9);
  for (auto v : p) {
    v -= c;
    cv::Point2f t(v.dot(ux), v.dot(uy));
    lo.x = std::min(lo.x, t.x);
    lo.y = std::min(lo.y, t.y);
    hi.x = std::max(hi.x, t.x);
    hi.y = std::max(hi.y, t.y);
  }
  auto mid = (lo + hi) / 2;
  float side = std::max(hi.x - lo.x, hi.y - lo.y) * expansion;
  require(side > 1, "degenerate face");
  c += ux * mid.x + uy * (mid.y + shift * side);
  double s = size / side;
  double a = std::acos(std::clamp(ux.x, -1.f, 1.f));
  if (ux.y < 0)
    a = -a;
  double co = std::cos(a), si = std::sin(a);
  return (cv::Mat_<float>(2, 3) << s * co, s * si,
          size / 2. - s * (co * c.x + si * c.y), -s * si, s * co,
          size / 2. - s * (-si * c.x + co * c.y));
}
std::vector<cv::Point2f> transform(const V &xy, const cv::Mat &inv,
                                   float scale = 1) {
  cv::Mat m;
  inv.convertTo(m, CV_64F);
  std::vector<cv::Point2f> p;
  for (size_t i = 0; i < xy.size(); i += 2) {
    double x = xy[i] * scale, y = xy[i + 1] * scale;
    p.emplace_back(
        m.at<double>(0, 0) * x + m.at<double>(0, 1) * y + m.at<double>(0, 2),
        m.at<double>(1, 0) * x + m.at<double>(1, 1) * y + m.at<double>(1, 2));
  }
  return p;
}
struct Source {
  V motion, kp, feature;
  cv::Mat affine;
};
struct Prep {
  std::unique_ptr<Net> det, l106;
  cv::Ptr<cv::FaceDetectorYN> yunet;
  Net l203, motion, appearance;
  Prep(const fs::path &r, bool gpuMotion = true)
      : l203(r / "landmark203.onnx", false),
        motion(r / "motion.onnx", gpuMotion),
        appearance(r / "appearance.onnx") {
    const auto manifest = avatar::readJson(r / "manifest.json");
    const auto profile = manifest.value("preparation", J::object()).value("profile", std::string());
    require(profile.empty() || profile == "yunet-203-v1", "unknown face preparation profile");
    if (profile == "yunet-203-v1")
      yunet = cv::FaceDetectorYN::create((r / "face_detection_yunet_2023mar.onnx").string(),
          "", {640,640}, .9f, .3f, 5000, cv::dnn::DNN_BACKEND_OPENCV, cv::dnn::DNN_TARGET_CPU);
    else {
      det = std::make_unique<Net>(r / "detector.onnx", false);
      l106 = std::make_unique<Net>(r / "landmark106.onnx", false);
    }
  }
  Source operator()(const Image &image) {
    cv::Mat im = rgb(image);
    cv::Mat a, inv, crop;
    if (yunet) {
      a = yunet_alignment::initialAffine(image, yunet);
    } else {
      int nw = 512, nh = int(512. * image.height / image.width);
      if (nh > 512) {
        nh = 512;
        nw = int(512. * image.width / image.height);
      }
      float ds = float(nh) / image.height;
      cv::Mat scaled, padded = cv::Mat::zeros(512, 512, CV_8UC3);
      cv::resize(im, scaled, {nw, nh});
      scaled.copyTo(padded(cv::Rect(0, 0, nw, nh)));
      // Upstream passes RGB into InsightFace's swapRB=True API.
      auto d =
          det->run({tensor(padded, 1.f / 128, true, 127.5)}, {{1, 3, 512, 512}});
      require(d.size() == 9, "detector outputs");
      struct Box {
        float x, y, r, b, score;
      };
      std::vector<Box> boxes;
      for (int k = 0; k < 3; ++k) {
        int stride = 8 << k, w = 512 / stride;
        for (size_t i = 0; i < d[k].size(); ++i)
          if (d[k][i] >= .5) {
            float x = (i / 2 % w) * stride, y = (i / 2 / w) * stride;
            auto &b = d[k + 3];
            boxes.push_back({(x - b[i * 4] * stride) / ds,
                             (y - b[i * 4 + 1] * stride) / ds,
                             (x + b[i * 4 + 2] * stride) / ds,
                             (y + b[i * 4 + 3] * stride) / ds, d[k][i]});
          }
      }
      require(!boxes.empty(), "未检测到人脸");
      std::sort(boxes.begin(), boxes.end(),
                [](auto a, auto b) { return a.score > b.score; });
      std::vector<Box> kept;
      for (auto b : boxes) {
        bool keep = true;
        for (auto a : kept) {
          float area =
              std::max(0.f, std::min(a.r, b.r) - std::max(a.x, b.x) + 1) *
              std::max(0.f, std::min(a.b, b.b) - std::max(a.y, b.y) + 1);
          if (area / ((a.r - a.x + 1) * (a.b - a.y + 1) +
                      (b.r - b.x + 1) * (b.b - b.y + 1) - area) >
              .4f) {
            keep = false;
            break;
          }
        }
        if (keep)
          kept.push_back(b);
      }
      auto b = *std::max_element(kept.begin(), kept.end(), [](auto a, auto b) {
        return (a.r - a.x) * (a.b - a.y) < (b.r - b.x) * (b.b - b.y);
      });
      double s = 192. / (std::max(b.r - b.x, b.b - b.y) * 1.5);
      a = (cv::Mat_<double>(2, 3) << s, 0, 96 - s * (b.x + b.r) / 2, 0, s,
                   96 - s * (b.y + b.b) / 2);
      cv::warpAffine(im, crop, a, {192, 192});
      auto p106 = l106->run({tensor(crop, 1, true)}, {{1, 3, 192, 192}})[0];
      require(p106.size() == 212, "106 shape");
      for (float &v : p106)
        v = (v + 1) * 96;
      cv::invertAffineTransform(a, inv);
      auto p = transform(p106, inv);
      a = cropMatrix(p, 224, 1.5, -.1);
    }
    cv::warpAffine(im, crop, a, {224, 224});
    auto o = l203.run({tensor(crop)}, {{1, 3, 224, 224}});
    require(o.size() >= 3 && o[2].size() == 406, "203 shape");
    cv::invertAffineTransform(a, inv);
    auto p = transform(o[2], inv, 224);
    a = cropMatrix(p, 512, 2.3, -.125);
    cv::warpAffine(im, crop, a, {512, 512});
    cv::Mat motionCrop;
    cv::resize(crop, motionCrop, {256, 256}, 0, 0, cv::INTER_AREA);
    auto input = tensor(motionCrop);
    auto m = motion.run({input}, {{1, 3, 256, 256}});
    require(m.size() == 7, "motion outputs");
    Source src;
    src.affine = a;
    src.motion = m[5];
    src.motion[0] -= 1;
    for (int k : {0, 1, 2, 3, 4})
      src.motion.insert(src.motion.end(), m[k].begin(), m[k].end());
    require(src.motion.size() == 265, "motion dimension");
    src.kp = m[6];
    src.feature = appearance.run({input}, {{1, 3, 256, 256}})[0];
    dump("source-motion", src.motion);
    dump("source-kp", src.kp);
    dump("source-feature", src.feature);
    return src;
  }
};
float degree(const float *p) {
  float mx = *std::max_element(p, p + 66), z = 0, v = 0;
  for (int i = 0; i < 66; ++i) {
    float e = std::exp(p[i] - mx);
    z += e;
    v += e * i;
  }
  return v / z * 3 - 97.5;
}
V points(const V &m, const V &kp) {
  require(m.size() == 265 && kp.size() == 63, "point inputs");
  float x = degree(m.data() + 1) * CV_PI / 180,
        y = degree(m.data() + 67) * CV_PI / 180,
        z = degree(m.data() + 133) * CV_PI / 180;
  cv::Matx33f rx(1, 0, 0, 0, cos(x), -sin(x), 0, sin(x), cos(x)),
      ry(cos(y), 0, sin(y), 0, 1, 0, -sin(y), 0, cos(y)),
      rz(cos(z), -sin(z), 0, sin(z), cos(z), 0, 0, 0, 1);
  auto r = (rz * ry * rx).t();
  V out(63);
  for (int i = 0; i < 21; ++i)
    for (int j = 0; j < 3; ++j) {
      float v = m[202 + i * 3 + j];
      for (int k = 0; k < 3; ++k)
        v += kp[i * 3 + k] * r(k, j);
      out[i * 3 + j] = v * (m[0] + 1) + (j < 2 ? m[199 + j] : 0);
    }
  return out;
}
V motionFromJson(const J &j) {
  V r = flatten(j.at("scale"));
  r[0] -= 1;
  for (auto k : {"pitch", "yaw", "roll", "t", "exp"}) {
    auto v = flatten(j.at(k));
    r.insert(r.end(), v.begin(), v.end());
  }
  require(r.size() == 265, "conditioning source");
  return r;
}
void sourceNeutralEmotion(J &cfg) {
  cfg["default_kwargs"]["emo"] = mouth_detail::neutralEmotionCondition();
}
void sourceCondition(J &cfg, const Source &source, const Image &image,
                     const fs::path &root) {
  const auto mode = cfg.value("sourceConditioning", std::string{});
  if (mode.empty()) return; // Original model retains its original contract.
  require(mode == "source-neutral-v1", "unsupported source conditioning");
  cv::Mat crop;
  cv::warpAffine(rgb(image), crop, source.affine, {512, 512});
  std::array<float,3> mouthGeometry;
  const auto eyes = condition_detail::eyeCondition(root / "face_landmarker.task", crop, &mouthGeometry);
  cfg["sourceMouthGeometry"] = mouthGeometry;
  auto slice = [&](int first, int last) {
    return J::array({V(source.motion.begin() + first, source.motion.begin() + last)});
  };
  J reference = {{"scale", J::array({J::array({source.motion[0] + 1})})},
                 {"pitch", slice(1, 67)}, {"yaw", slice(67, 133)},
                 {"roll", slice(133, 199)}, {"t", slice(199, 202)},
                 {"exp", slice(202, 265)}, {"kp", J::array({source.kp})}};
  auto &def = cfg["default_kwargs"];
  sourceNeutralEmotion(cfg);
  def["ch_info"] = {{"x_s_info_lst", J::array({reference})},
                    {"sc", source.kp},
                    {"eye_open_lst", J::array({J::array({eyes[0], eyes[1]})})},
                    {"eye_ball_lst", J::array({V(eyes.begin() + 2, eyes.end())})}};
  cfg["mouthConditionReference"] = true;
}
// Use explicit Euler angles after official delta_pitch, without re-quantizing
// logits.
V pointsAngles(const V &m, const V &kp, float pitch, float yaw, float roll) {
  float x = pitch * CV_PI / 180, y = yaw * CV_PI / 180, z = roll * CV_PI / 180;
  cv::Matx33f rx(1, 0, 0, 0, cos(x), -sin(x), 0, sin(x), cos(x)),
      ry(cos(y), 0, sin(y), 0, 1, 0, -sin(y), 0, cos(y)),
      rz(cos(z), -sin(z), 0, sin(z), cos(z), 0, 0, 0, 1);
  auto r = (rz * ry * rx).t();
  V out(63);
  for (int i = 0; i < 21; ++i)
    for (int j = 0; j < 3; ++j) {
      float v = m[202 + i * 3 + j];
      for (int k = 0; k < 3; ++k)
        v += kp[i * 3 + k] * r(k, j);
      out[i * 3 + j] = v * (m[0] + 1) + (j < 2 ? m[199 + j] : 0);
    }
  return out;
}
void produceAudioFeatures(const fs::path &root, const V &audio, V &features,
                          const std::function<void(size_t)> &before = {},
                          const std::function<void(size_t)> &publish = {},
                          int cooperativeWorkers = 0) {
  Net net(root / "hubert.onnx", false, cooperativeWorkers);
  size_t n = (audio.size() + 639) / 640;
  require(features.size() == n * 1024, "audio feature storage");
  for (size_t i = 0; i < n; i += 5) {
    if (before) before(i);
    V chunk(6480);
    int64_t start = int64_t(i) * 640 - 2000;
    for (int j = 0; j < 6480; ++j) {
      int64_t k = start + j;
      if (k >= 0 && k < int64_t(audio.size()))
        chunk[j] = audio[k];
    }
    auto y = net.run({chunk}, {{1, 6480}})[0];
    require(y.size() % 1024 == 0 && y.size() >= 14 * 1024, "HuBERT shape");
    size_t off = y.size() / 1024 - 14;
    for (size_t j = 0; j < 5 && i + j < n; ++j)
      for (int k = 0; k < 1024; ++k)
        features[(i + j) * 1024 + k] =
            (y[(off + j * 2) * 1024 + k] + y[(off + j * 2 + 1) * 1024 + k]) *
            .5f;
    if (publish) publish(std::min(i + 5, n));
    avatar::progress("ditto-audio", std::min(i + 5, n), n);
  }
  dump("hubert", features);
}
V audioFeatures(const fs::path &root, const V &audio) {
  V features(((audio.size() + 639) / 640) * 1024);
  produceAudioFeatures(root, audio, features);
  return features;
}
size_t mirror(size_t i, size_t n) {
  require(n > 0, "empty conditioning");
  return i / n % 2 ? n - 1 - i % n : i % n;
}
V conditions(const V &features, const J &cfg, size_t start = 0,
             size_t count = 0) {
  auto &def = cfg.at("default_kwargs");
  auto &ch = def.at("ch_info");
  auto emo = flatten(def.at("emo")), eye = flatten(ch.at("eye_open_lst")),
       ball = flatten(ch.at("eye_ball_lst")), sc = flatten(ch.at("sc"));
  require(sc.size() == 63 && emo.size() % 8 == 0 && eye.size() % 2 == 0 &&
              ball.size() % 6 == 0,
          "condition dimensions");
  size_t n = features.size() / 1024;
  if (!count) count = n;
  V cond;
  cond.reserve(count * 1103);
  for (size_t offset = 0; offset < count; ++offset) {
    const size_t i = std::min(n - 1, start + offset);
    cond.insert(cond.end(), features.begin() + i * 1024,
                features.begin() + (i + 1) * 1024);
    size_t e = i % (emo.size() / 8), o = mirror(i, eye.size() / 2),
           b = mirror(i, ball.size() / 6);
    cond.insert(cond.end(), emo.begin() + e * 8, emo.begin() + (e + 1) * 8);
    cond.insert(cond.end(), eye.begin() + o * 2, eye.begin() + (o + 1) * 2);
    cond.insert(cond.end(), ball.begin() + b * 6, ball.begin() + (b + 1) * 6);
    cond.insert(cond.end(), sc.begin(), sc.end());
  }
  return cond;
}
void smooth(V &x, size_t s, size_t e) {
  auto copy = x;
  size_t n = x.size() / 265;
  e = std::min(e, n);
  for (size_t i = s; i < e; ++i) {
    size_t a = i ? i - 1 : 0, b = std::min(n, i + 2);
    for (int k = 0; k < 202; ++k) {
      float sum = 0;
      for (size_t j = a; j < b; ++j)
        sum += copy[j * 265 + k];
      x[i * 265 + k] = sum / float(b - a);
    }
  }
}
V predictWindows(const fs::path &root, size_t n, const J &cfg, int seed,
                 const std::function<V(size_t)> &window,
                 const std::function<void(const V &, size_t)> &publish = {},
                 const std::function<void()> &check = {},
                 int cooperativeWorkers = 0, DenoiseStats *reported = nullptr) {
  const size_t sz = 80 * 265;
  require(n > 0, "empty motion");
  DenoiseStats stats;
  Denoiser net(root, cooperativeWorkers, stats);
  auto sch = avatar::readJson(root / "schedule.json");
  auto times = sch.at("times").get<std::vector<int64_t>>();
  auto alpha = flatten(sch.at("alpha")), sigma = flatten(sch.at("sigma")),
       cs = flatten(sch.at("c"));
  require(times.size() == 50 && alpha.size() == 49, "diffusion schedule");
  std::mt19937 rng(seed);
  std::normal_distribution<float> normal;
  V noise(49 * sz), initial;
  for (float &v : noise)
    v = normal(rng);
  if (const char *d = std::getenv("XINSHEN_DITTO_REFERENCE")) {
    noise = read(fs::path(d) / "step-noise.f32", 49 * sz);
    initial = read(fs::path(d) / "initial-noise.f32", ((n + 69) / 70) * sz);
  }
  V source = motionFromJson(
        cfg.at("default_kwargs").at("ch_info").at("x_s_info_lst").at(0)),
    kp = source, res;
  auto bounds = cfg.at("default_kwargs").at("v_min_max_for_clip");
  auto lo = flatten(bounds[0]), hi = flatten(bounds[1]);
  require(lo.size() == 265 && hi.size() == 265, "clip bounds");
  auto clip = [&](V &motion, size_t end) {
    for (size_t i = 0; i < end; ++i)
      for (int j = 0; j < 265; ++j)
        motion[i * 265 + j] = std::clamp(motion[i * 265 + j], lo[j], hi[j]);
  };
  for (size_t start = 0, clip = 0; start < n; start += 70, ++clip) {
    if (check) check();
    V c = window(start);
    require(c.size() == 80 * 1103, "conditioning window");
    net.prepare(c);
    V x(sz);
    if (initial.empty())
      for (float &v : x)
        v = normal(rng);
    else
      std::copy_n(initial.begin() + clip * sz, sz, x.begin());
    for (size_t step = 0; step < 50; ++step) {
      if (check) check();
      auto pred = net.run(x, kp, c, times[step]);
      require(pred.size() == 2 && pred[0].size() == sz && pred[1].size() == sz,
              "denoiser outputs");
      if (clip == 0 && step == 0) {
        dump("denoise-pred-noise", pred[0]);
        dump("denoise-x-start", pred[1]);
      }
      if (step == 49)
        x = std::move(pred[1]);
      else
        for (size_t j = 0; j < sz; ++j)
          x[j] = pred[1][j] * alpha[step] + cs[step] * pred[0][j] +
                 sigma[step] * noise[step * sz + j];
    }
    if (res.empty()) {
      res = std::move(x);
      smooth(res, 0, 80);
    } else {
      size_t len = res.size() / 265;
      for (int i = 0; i < 10; ++i)
        for (int j = 0; j < 265; ++j) {
          float a = i / 10.f;
          res[(len - 10 + i) * 265 + j] =
              res[(len - 10 + i) * 265 + j] * (1 - a) + x[i * 265 + j] * a;
        }
      res.insert(res.end(), x.begin() + 10 * 265, x.end());
      smooth(res, len - 10, len + 1);
    }
    size_t index = res.size() / 265 - 11;
    kp = source;
    std::copy_n(res.begin() + index * 265, 202, kp.begin());
    if (publish && start + 70 < n) {
      // A future overlap can still alter the last ten local-smoothed frames.
      // Final smoothing needs one more neighbor. Publish only indices < L-11
      // from a copy; the recurrent kp must use the original local-smoothed res.
      const size_t stable = std::min(n, res.size() / 265 - 11);
      V ready = res;
      smooth(ready, 0, stable);
      for (size_t i = 0; i < stable; ++i)
        for (int j = 0; j < 265; ++j)
          ready[i * 265 + j] = std::clamp(ready[i * 265 + j], lo[j], hi[j]);
      publish(ready, stable);
    }
    avatar::progress("ditto-motion", std::min(start + 70, n), n);
  }
  res.resize(n * 265);
  smooth(res, 0, n);
  dump("raw-motion", res);
  clip(res, n);
  if (publish) publish(res, n);
  if (reported) *reported = stats;
  return res;
}
V predict(const fs::path &root, const V &features, const J &cfg, int seed,
          DenoiseStats *stats = nullptr) {
  return predictWindows(root, features.size() / 1024, cfg, seed,
                        [&](size_t start) { return conditions(features, cfg, start, 80); },
                        {}, {}, 0, stats);
}
std::vector<int> blinks(size_t n, int seed) {
  std::mt19937 rng(seed);
  std::uniform_int_distribution<int> uniform(60, 100);
  std::vector<int> indices(n);
  size_t first = uniform(rng), last = uniform(rng),
         limit = n > std::max<size_t>(last, 15) ? n - std::max<size_t>(last, 15)
                                                : 0;
  for (size_t i = first; i < limit; i += 15 + uniform(rng))
    for (int j = 0; j < 15 && i + j < n; ++j)
      indices[i + j] = j;
  if (const char *d = std::getenv("XINSHEN_DITTO_REFERENCE")) {
    auto p = fs::path(d) / "blink.json";
    if (fs::exists(p)) {
      indices = avatar::readJson(p).get<std::vector<int>>();
      require(indices.size() == n, "blink fixture length");
    }
  }
  return indices;
}
V stitchMotion(const fs::path &root, const V &raw, const Source &source,
               const J &cfg, int seed, bool still = true,
               int64_t onlyFrame = -1, Net *shared = nullptr,
               float fixedRatio = -1, size_t rangeFirst = 0,
               size_t rangeEnd = 0, const std::vector<int> *fullBlink = nullptr) {
  std::unique_ptr<Net> owned;
  if (!shared) {
    owned = std::make_unique<Net>(root / "stitch.onnx", false);
    shared = owned.get();
  }
  size_t n = raw.size() / 265;
  auto xs = points(source.motion, source.kp);
  V output;
  output.reserve(n * 63);
  auto def = cfg.at("default_kwargs");
  auto reference = motionFromJson(def.at("ch_info").at("x_s_info_lst").at(0));
  if (cfg.contains("mouthReferenceExp")) {
    const auto mouth = cfg.at("mouthReferenceExp").get<V>();
    require(mouth.size() == 18, "mouth reference size");
    finite(mouth);
    size_t index = 0;
    for (int p : mouth_detail::points)
      for (int j = 0; j < 3; ++j) reference[202 + p * 3 + j] = mouth[index++];
  }
  float ratio =
      fixedRatio > 0 ? fixedRatio : (reference[0] + 1) / (source.motion[0] + 1);
  auto eye = flatten(def.at("delta_eye_arr"));
  require(eye.size() == 15 * 63, "blink coefficients");
  const auto ownedBlink = fullBlink ? std::vector<int>{} : blinks(n, seed);
  const auto &blink = fullBlink ? *fullBlink : ownedBlink;
  require(blink.size() == n, "blink sequence length");
  float pitchS = degree(source.motion.data() + 1),
        yawS = degree(source.motion.data() + 67);
  for (size_t i = onlyFrame < 0 ? rangeFirst : size_t(onlyFrame);
       i < (onlyFrame < 0 ? (rangeEnd ? rangeEnd : n) : size_t(onlyFrame) + 1); ++i) {
    V m = source.motion;
    if (still)
      for (int k = 1; k < 202; ++k)
        m[k] += (raw[i * 265 + k] - raw[k]) * (k < 199 ? ratio : 1) *
                control_detail::gain(cfg, k < 67 ? "pitch" : k < 133 ? "yaw" : k < 199 ? "roll" : "t");
    for (int p : {6, 12, 14, 17, 19, 20})
      for (int j = 0; j < 3; ++j) {
        int k = 202 + p * 3 + j;
        m[k] += (raw[i * 265 + k] -
                 (cfg.value("mouthConditionReference", false) ? reference[k] : raw[k])) * ratio *
                mouth_detail::openingGain(cfg, i) * control_detail::gain(cfg, "exp");
      }
    if (still)
      for (int p : {11, 13, 15, 16, 18})
        for (int j = 0; j < 3; ++j)
          m[202 + p * 3 + j] += eye[blink[i] * 63 + p * 3 + j];
    float pitch = degree(m.data() + 1) + 2, yaw = degree(m.data() + 67),
          roll = degree(m.data() + 133);
    if (still) {
      float dx = (yaw - yawS) * .26f, dy = (pitch - pitchS) * .28f;
      m[202 + 33] += dx * (dx > 0 ? .0007f : .001f);
      m[202 + 45] += dx * (dx > 0 ? .001f : .0007f);
      m[202 + 34] -= dy * .001f;
      m[202 + 46] -= dy * .001f;
    }
    auto xd = pointsAngles(m, source.kp, pitch, yaw, roll);
    auto y = shared->run({xs, xd}, {{1, 21, 3}, {1, 21, 3}})[0];
    require(y.size() == 63, "stitch output");
    output.insert(output.end(), y.begin(), y.end());
  }
  if (onlyFrame < 0 && !rangeEnd)
    dump("points", output);
  return output;
}
// Fixed storage and published prefixes allow the CPU audio/motion stages to
// overlap rendering without exposing mutable recurrent diffusion state.
int streamingCpuWorkers(const char *stageVariable) {
  int workers = std::strcmp(stageVariable, "XINSHEN_DITTO_STREAM_AUDIO_CPU_THREADS") == 0 ? 2 : 4;
  const char *variable = stageVariable;
  const char *configured = std::getenv(variable);
  if (!configured) {
    variable = "XINSHEN_DITTO_STREAM_CPU_THREADS";
    configured = std::getenv(variable);
  }
  if (configured) {
    char *end = nullptr;
    const auto parsed = std::strtol(configured, &end, 10);
    require(end != configured && !*end && parsed >= 1 && parsed <= 8,
            std::string(variable) + " must be between 1 and 8");
    workers = int(parsed);
  }
  return std::min(workers, int(std::max(1u, std::thread::hardware_concurrency())));
}
class MotionStream {
 public:
  MotionStream(const fs::path &root, const V &audio, const Source &source,
               const J &cfg, int seed, Clock::time_point origin)
      : root_(root), audio_(audio), source_(source), cfg_(cfg), seed_(seed),
        origin_(origin), n_((audio.size() + 639) / 640),
        audioWorkers_(streamingCpuWorkers("XINSHEN_DITTO_STREAM_AUDIO_CPU_THREADS")),
        motionWorkers_(streamingCpuWorkers("XINSHEN_DITTO_STREAM_MOTION_CPU_THREADS")),
        features_(n_ * 1024), raw_(n_ * 265), driven_(n_ * 63) {}
  ~MotionStream() { stop(); join(); }
  void start() {
    try {
      audioThread_ = std::thread([this] {
        InferencePriority priority("audio");
        try {
          const auto started = Clock::now();
          auto chunk = started;
          produceAudioFeatures(root_, audio_, features_,
              [&](size_t first) {
                std::unique_lock<std::mutex> lock(mutex_);
                wait(lock, [&] { return drain_ || first < featureDemand_ + 160; });
                chunk = Clock::now();
              },
              [&](size_t end) {
                audioComputeSeconds_ += seconds(chunk);
                std::lock_guard<std::mutex> lock(mutex_);
                featuresReady_ = end;
                changed_.notify_all();
              }, audioWorkers_);
          audioWallSeconds_ = seconds(started);
        } catch (...) { fail(); }
      });
      motionThread_ = std::thread([this] {
        InferencePriority priority("motion");
        try {
          const auto started = Clock::now();
          auto windowStarted = started;
          Net stitch(root_ / "stitch.onnx", false, 1);
          const auto blink = blinks(n_, seed_);
          size_t published = 0;
          auto finalRaw = predictWindows(root_, n_, cfg_, seed_,
              [&](size_t first) {
                const size_t needed = std::min(n_, first + 80);
                {
                  std::unique_lock<std::mutex> lock(mutex_);
                  // Limit compute ahead, not only queue memory. Two 70-frame
                  // windows suffice while the renderer owns the slower stage.
                  wait(lock, [&] { return drain_ || first + 70 <= motionDemand_ + 140; });
                  featureDemand_ = needed;
                  changed_.notify_all();
                  wait(lock, [&] { return featuresReady_ >= needed; });
                }
                auto result = conditions(features_, cfg_, first, 80);
                windowStarted = Clock::now();
                return result;
              },
              [&](const V &ready, size_t end) {
                motionComputeSeconds_ += seconds(windowStarted);
                check();
                require(end >= published && end <= n_, "stream motion prefix");
                std::copy(ready.begin() + published * 265, ready.begin() + end * 265,
                          raw_.begin() + published * 265);
                const auto stitchStart = Clock::now();
                auto points = stitchMotion(root_, raw_, source_, cfg_, seed_, true,
                                           -1, &stitch, -1, published, end, &blink);
                stitchSeconds_ += seconds(stitchStart);
                require(points.size() == (end - published) * 63, "stream points prefix");
                std::copy(points.begin(), points.end(), driven_.begin() + published * 63);
                {
                  std::lock_guard<std::mutex> lock(mutex_);
                  if (!motionReady_) firstMotionSeconds_ = seconds(origin_);
                  motionReady_ = end;
                  changed_.notify_all();
                }
                published = end;
              }, [this] { check(); }, motionWorkers_, &denoiseStats_);
          require(published == n_ && finalRaw == raw_, "stream final motion differs from emitted prefix");
          dump("points", driven_);
          motionWallSeconds_ = seconds(started);
          allMotionSeconds_ = seconds(origin_);
        } catch (...) { fail(); }
      });
    } catch (...) {
      stop();
      join();
      throw;
    }
  }
  V pointsAt(size_t frame) {
    require(frame < n_, "stream frame index");
    std::unique_lock<std::mutex> lock(mutex_);
    motionDemand_ = std::max(motionDemand_, frame + 1);
    changed_.notify_all();
    wait(lock, [&] { return motionReady_ > frame; });
    return V(driven_.begin() + frame * 63, driven_.begin() + (frame + 1) * 63);
  }
  void check() {
    avatar::checkCancelled();
    std::lock_guard<std::mutex> lock(mutex_);
    checkLocked();
  }
  void stop(std::exception_ptr reason = {}) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (reason && !failure_) failure_ = reason;
    stopped_ = true;
    changed_.notify_all();
  }
  void complete() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      drain_ = true; // Covered audio may end before the last timeline frame.
      changed_.notify_all();
    }
    join();
    check();
    require(motionReady_ == n_, "stream motion incomplete");
  }
  void store(J &cache) const {
    cache["rawMotion"] = raw_;
    cache["drivingPoints"] = driven_;
    cache["timingsSeconds"]["audioFeatures"] = audioComputeSeconds_;
    cache["timingsSeconds"]["motionSampling"] = motionComputeSeconds_;
    cache["timingsSeconds"]["stitching"] = stitchSeconds_;
    cache["streamingWallSeconds"] = {{"audioThread", audioWallSeconds_},
                                    {"motionThread", motionWallSeconds_},
                                    {"firstMotionReady", firstMotionSeconds_},
                                    {"allMotionReady", allMotionSeconds_}};
    cache["streaming"] = true;
    cache["streamingAudioCpuWorkers"] = audioWorkers_;
    cache["streamingMotionCpuWorkers"] = motionWorkers_;
    cache["streamingCpuWorkersPerStage"] = audioWorkers_ == motionWorkers_ ? J(audioWorkers_) : J(nullptr);
    cache["streamingCpuSpinning"] = false;
    cache["denoiseConditionManifestSha256"] = denoiseStats_.manifestSha256;
    cache["denoiseSubtimingsSeconds"] = denoiseStats_.json();
  }
 private:
  template <class Ready>
  void wait(std::unique_lock<std::mutex> &lock, Ready ready) {
    while (true) {
      avatar::checkCancelled();
      checkLocked();
      if (ready()) return;
      changed_.wait_for(lock, std::chrono::milliseconds(50));
    }
  }
  void checkLocked() {
    if (failure_) std::rethrow_exception(failure_);
    require(!stopped_, "stream stopped");
  }
  void fail() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!failure_) failure_ = std::current_exception();
    stopped_ = true;
    changed_.notify_all();
  }
  void join() {
    if (audioThread_.joinable()) audioThread_.join();
    if (motionThread_.joinable()) motionThread_.join();
  }
  fs::path root_;
  const V &audio_;
  const Source &source_;
  const J &cfg_;
  int seed_;
  Clock::time_point origin_;
  size_t n_;
  int audioWorkers_, motionWorkers_;
  V features_, raw_, driven_;
  std::mutex mutex_;
  std::condition_variable changed_;
  std::exception_ptr failure_;
  std::thread audioThread_, motionThread_;
  size_t featuresReady_ = 0, motionReady_ = 0, featureDemand_ = 0, motionDemand_ = 0;
  bool stopped_ = false, drain_ = false;
  double audioComputeSeconds_ = 0, motionComputeSeconds_ = 0, stitchSeconds_ = 0;
  double audioWallSeconds_ = 0, motionWallSeconds_ = 0;
  double firstMotionSeconds_ = 0, allMotionSeconds_ = 0;
  DenoiseStats denoiseStats_;
};
cv::Mat dittoMask() {
  cv::Mat mask(512, 512, CV_32F);
  for (int y = 0; y < 512; ++y)
    for (int x = 0; x < 512; ++x) {
      float dx = x < 26     ? 1 - x / 25.f
                 : x >= 486 ? (x - 486) / 25.f
                            : 0,
            dy = y < 26     ? 1 - y / 25.f
                 : y >= 486 ? (y - 486) / 25.f
                            : 0;
      mask.at<float>(y, x) = 1 - std::min(1.f, std::sqrt(dx * dx + dy * dy));
    }
  return mask;
}
// Image sources reuse their exact floating-point warp mask. Video sources
// refresh this cache whenever their affine transform changes.
struct CompositeCache {
  cv::Mat affine, inverse, mask;
  int width = 0, height = 0;
  void prepare(const Source &s, const Image &image) {
    if (width == image.width && height == image.height && !affine.empty() &&
        cv::norm(affine, s.affine, cv::NORM_INF) == 0)
      return;
    affine = s.affine.clone();
    width = image.width;
    height = image.height;
    cv::invertAffineTransform(affine, inverse);
    cv::warpAffine(dittoMask(), mask, inverse, {width, height});
  }
};
Image composite(Image image, const Source &s, const V &face,
                CompositeCache &cache) {
  require(face.size() == 3 * 512 * 512, "generator output");
  cv::Mat rendered(512, 512, CV_32FC3);
  for (int y = 0; y < 512; ++y)
    for (int x = 0; x < 512; ++x)
      for (int c = 0; c < 3; ++c)
        rendered.at<cv::Vec3f>(y, x)[2 - c] =
            std::clamp(face[c * 512 * 512 + y * 512 + x], 0.f, 1.f) * 255;
  cache.prepare(s, image);
  cv::Mat warped;
  cv::warpAffine(rendered, warped, cache.inverse, {image.width, image.height});
  const auto &mask = cache.mask;
  for (int y = 0; y < image.height; ++y)
    for (int x = 0; x < image.width; ++x) {
      float a = std::clamp(mask.at<float>(y, x), 0.f, 1.f);
      if (!image.alpha.empty())
        a *= image.alpha[y * image.width + x] / 255.f;
      for (int c = 0; c < 3; ++c)
        image.pixel(x, y)[c] = uint8_t(std::clamp(
            a * warped.at<cv::Vec3f>(y, x)[c] + (1 - a) * image.pixel(x, y)[c],
            0.f, 255.f));
    }
  return image;
}
J sourceJson(const Source &s) {
  V a;
  for (int i = 0; i < 6; ++i)
    a.push_back(s.affine.at<float>(i / 3, i % 3));
  return {{"motion", s.motion}, {"kp", s.kp}, {"affine", a}};
}
Source sourceRead(const J &j) {
  Source s;
  s.motion = j.at("motion").get<V>();
  s.kp = j.at("kp").get<V>();
  auto a = j.at("affine").get<V>();
  require(s.motion.size() == 265 && s.kp.size() == 63 && a.size() == 6,
          "source cache");
  finite(s.motion);
  finite(s.kp);
  finite(a);
  s.affine = cv::Mat(2, 3, CV_32F);
  std::copy(a.begin(), a.end(), s.affine.ptr<float>());
  return s;
}
void checkDriver(const J &plan, const J &caps) {
  if (!plan.contains("audioDriver"))
    return;
  const auto &driver = plan.at("audioDriver");
  require(driver.value("engine", "") == "ditto-native-v1" &&
              driver.value("execution", "") == "local",
          "需要本机 Ditto 驱动配置");
  if (driver.contains("mouthReferenceCatalogSha256"))
    require(driver.at("mouthReferenceCatalogSha256") == caps.at("mouthReferenceCatalogSha256"),
            "口型适配配置已变化，请重新检查本机模型");
  if (driver.contains("mouthReferenceRevision"))
    require(driver.at("mouthReferenceRevision") == mouth_detail::runtimeRevision,
            "口型适配程序已变化，请重新生成");
  const auto identity = driver.value("pipelineId", "");
  require(identity.empty() || identity == caps.at("pipelineId"),
          "Ditto 模型版本已变化，请重新生成口播");
}
fs::path mouthCalibrationDirectory() {
  if (const char *value=std::getenv("XINSHEN_DITTO_MOUTH_CACHE_DIR")) {
    require(*value, "empty mouth cache override");
    return fs::path(value);
  }
  const char *userHome=std::getenv("HOME");
  require(userHome && *userHome, "mouth cache home unavailable");
#if defined(__APPLE__)
  return fs::path(userHome)/"Library/Caches/MossStudio/MossStudio/ditto-mouth-v2";
#else
  const char *xdg=std::getenv("XDG_CACHE_HOME");
  return (xdg && *xdg ? fs::path(xdg) : fs::path(userHome)/".cache")/"moss/ditto-mouth-v2";
#endif
}
std::string jsonHash(const J &value) {
  const auto bytes=value.dump();
  return avatar::sha256(bytes.data(),bytes.size());
}
void saveMouthCache(const fs::path &path, const J &value) {
  fs::path temp;
  try {
    mouth_cache::Lock lock(path.parent_path());
    mouth_cache::prune(path.parent_path());
    temp=path.string()+"."+std::to_string(std::random_device{}())+"."+
         std::to_string(Clock::now().time_since_epoch().count())+".partial";
    avatar::writeJson(temp, {{"value",value},{"sha256",jsonHash(value)}});
    avatar::checkCancelled();
    avatar::replaceFile(temp,path);
    mouth_cache::prune(path.parent_path());
  } catch (const std::exception &) {
    std::error_code ignored;
    if (!temp.empty()) fs::remove(temp,ignored);
    avatar::checkCancelled(); // Cancellation must never become a fallback/cache miss.
    // Read-only or full user cache does not invalidate an in-memory calibration.
  }
}
J loadMouthCache(const fs::path &path) {
  try {
    mouth_cache::Lock lock(path.parent_path());
    // A legacy oversized entry must not be parsed into memory.
    if (fs::is_regular_file(fs::symlink_status(path)) && fs::file_size(path)<=mouth_cache::limitBytes) {
    auto saved=avatar::readJson(path);
    if (saved.at("sha256")==jsonHash(saved.at("value"))) {
      mouth_cache::touch(path);
      mouth_cache::prune(path.parent_path());
      return saved.at("value");
    }
    }
    mouth_cache::prune(path.parent_path());
  } catch (const std::exception &) { avatar::checkCancelled(); }
  return nullptr;
}
J configureMouthReference(const fs::path &root, const J &plan, const J &caps,
                          const J &av, const Source &source, J &cfg, const V &audio) {
  cfg.erase("mouthOpeningTransition");
  cfg.erase("mouthCalibrationRuntime");
  J catalog;
  try {
    if (fs::exists(root / "mouth-reference.json"))
      catalog = avatar::readJson(root / "mouth-reference.json");
  } catch (const std::exception &) { /* Optional policy fails back to source. */ }
  auto decision = mouth_detail::select(
      plan.value("audioDriver", J::object()).value("mouthReference", "auto"),
      catalog, caps.at("mouthReferenceCatalogSha256"), av.at("sourceSha256"),
      caps.at("pipelineId"), av.at("kind") == "image", sourceJson(source), cfg);
  if (decision.at("mode")!="auto" || av.at("kind")!="image") return decision;
  const auto automatic=catalog.is_object() ? catalog.value("automaticCalibration",J()) : J();
  if (!automatic.is_object() || automatic.value("revision",J())!=mouth_detail::automaticRevision ||
      catalog.value("pipelineId",J())!=caps.at("pipelineId") ||
      cfg.value("sourceConditioning","")!="source-neutral-v1" || !cfg.value("mouthConditionReference",false))
    return decision;
  decision["automaticRevision"]=mouth_detail::automaticRevision;
  decision["referenceRevision"]=mouth_detail::runtimeRevision;
  const auto emotion=cfg.at("default_kwargs").at("emo");
  const bool emotionLinked=!mouth_detail::isNeutralEmotion(emotion);
  decision["emotionCondition"]=emotion;
  // Preserve previously reviewed partial corrections for the built-in relaxed
  // portraits. Generic calibration must not silently promote a 50% profile.
  if (decision.at("strategy")=="calibrated" && decision.at("strength").get<double>()<1)
    return decision;
  const auto rejected=mouth_detail::geometryRejection(cfg.value("sourceMouthGeometry",J()),
      degree(source.motion.data()+67),degree(source.motion.data()+1));
  if (!rejected.empty()) { decision["automaticFallback"]=rejected; return decision; }
  const auto start=Clock::now();
  try {
    const auto cacheDir=mouthCalibrationDirectory();
    const J baseIdentity={{"revision",mouth_detail::runtimeRevision}, {"pipelineId",caps.at("pipelineId")},
      {"conditionManifestSha256",caps.at("denoiseConditionManifestSha256")},
      {"catalogSha256",caps.at("mouthReferenceCatalogSha256")}, {"sourceSha256",av.at("sourceSha256")},
      {"source",sourceJson(source)}, {"sourceMouthGeometry",cfg.at("sourceMouthGeometry")},
      {"configSha256",avatar::sha256(root/"config.json")}};
    V features;
    const auto loadFeatures=[&] {
      if (!features.empty()) return;
      const auto featureKey=avatar::sha256(root/"hubert.onnx")+std::string("-native-silence150-v1");
      const auto featurePath=cacheDir/(featureKey+".json");
      auto featuresSaved=loadMouthCache(featurePath);
      if (featuresSaved.is_object() && featuresSaved.value("key","")==featureKey) {
        try {
          features=featuresSaved.at("features").get<V>();
          finite(features);
          if (features.size()!=150*1024) features.clear();
        } catch (const std::exception &) { avatar::checkCancelled(); features.clear(); }
      }
      if (features.empty()) {
        features=audioFeatures(root,V(150*640,0));
        saveMouthCache(featurePath,{{"key",featureKey},{"features",features}});
      }
    };
    const auto referenceFor=[&](const J &predictionCfg, bool &hit, std::string &key) {
      auto identity=baseIdentity;
      identity["defaultKwargs"]=predictionCfg.at("default_kwargs");
      key=jsonHash(identity);
      const auto path=cacheDir/(key+".json");
      auto saved=loadMouthCache(path);
      hit=saved.is_object() && saved.value("key","")==key && saved.contains("summary");
      if (hit) {
        const auto why=mouth_detail::silenceStabilityRejection(saved.at("summary"));
        if (why!="invalid-silence-reference" && why!="invalid-silence-statistics")
          return saved.at("summary");
        hit=false;
      }
      avatar::checkCancelled();
      loadFeatures();
      std::vector<V> runs;
      for (int calibrationSeed : mouth_detail::calibrationSeeds) {
        avatar::checkCancelled();
        avatar::progress("ditto-mouth-calibration",runs.size(),3);
        runs.push_back(predict(root,features,predictionCfg,calibrationSeed));
      }
      auto summary=mouth_detail::summarizeSilence(runs,mouth_detail::mouthOf(source.motion));
      saveMouthCache(path,{{"key",key},{"identity",identity},{"summary",summary}});
      avatar::progress("ditto-mouth-calibration",3,3);
      return summary;
    };
    bool hit=false, neutralHit=false;
    std::string key, neutralKey;
    J neutral;
    if (emotionLinked) {
      auto neutralCfg=cfg;
      sourceNeutralEmotion(neutralCfg);
      neutral=referenceFor(neutralCfg,neutralHit,neutralKey);
    }
    const auto summary=referenceFor(cfg,hit,key);
    cfg["mouthCalibrationRuntime"]={{"cacheHit",hit},{"seconds",seconds(start)},{"key",key}};
    if (emotionLinked) {
      cfg["mouthCalibrationRuntime"]["neutralCacheHit"]=neutralHit;
      cfg["mouthCalibrationRuntime"]["neutralKey"]=neutralKey;
      decision["neutralCalibrationKey"]=neutralKey;
      decision["neutralCalibration"]=neutral;
    }
    const auto why=emotionLinked
        ? mouth_detail::emotionCalibrationRejection(summary,neutral,mouth_detail::mouthOf(source.motion))
        : mouth_detail::calibrationRejection(summary,mouth_detail::mouthOf(source.motion));
    if (!why.empty()) { decision["automaticFallback"]=why; decision["calibrationKey"]=key; return decision; }
    const auto opening=mouth_detail::openingTransition(audio);
    cfg["mouthReferenceExp"]=summary.at("referenceMouth");
    cfg["mouthOpeningTransition"]=opening;
    decision["revision"]=mouth_detail::runtimeRevision;
    decision["strategy"]="silent-calibrated";
    decision["profileId"]="";
    decision["strength"]=1;
    decision["reason"]=emotionLinked ? "emotion-conditioned-stable-silence" : "source-conditioned-stable-silence";
    if (emotionLinked) decision["emotionOffsetMax"]=mouth_detail::emotionOffsetMax(summary,neutral);
    decision["calibrationKey"]=key;
    decision["calibration"]=summary;
    decision["openingTransition"]=cfg.at("mouthOpeningTransition");
  } catch (const std::exception &e) {
    avatar::checkCancelled();
    decision["automaticFallback"]="calibration-unavailable";
    cfg["mouthCalibrationRuntime"]={{"cacheHit",false},{"seconds",seconds(start)}};
    std::fprintf(stderr,"[ditto_mouth] calibration fallback: %s\n",e.what());
  }
  return decision;
}
J build(const fs::path &root, const fs::path &dir, const fs::path &planPath,
        const fs::path &out) {
  auto started = Clock::now();
  J timings;
  auto caps = capabilities(root);
  auto plan = avatar::readJson(planPath);
  checkDriver(plan, caps);
  auto av = avatar::readJson(dir / "avatar.json");
  const bool still = av.at("kind") == "image";
  auto src = avatar::member(dir, av.at("source"));
  require(avatar::sha256(src) == av.at("sourceSha256"), "source changed");
  auto audio = avatar::readAudioPlan(planPath);
  require(!audio.samples.empty(), "empty audio");
  int seed = plan.value(
      "seed", plan.value("audioDriver", J::object()).value("seed", 1024));
  auto cfg = avatar::readJson(root / "config.json");
  const auto controls = control_detail::normalize(plan.value("audioDriver", J::object()));
  timings["validationAndAudioPlan"] = seconds(started);
  started = Clock::now();
  Source s;
  {
    Prep prep(root, !still);
    VideoReader reader(src);
    Image image;
    require(reader.next(image), "source decode");
    s = prep(image);
    sourceCondition(cfg, s, image, root);
  }
  control_detail::apply(cfg, controls);
  timings["sourcePreparation"] = seconds(started);
  const auto mouthReference = configureMouthReference(root, plan, caps, av, s, cfg, audio.samples);
  started = Clock::now();
  auto features = audioFeatures(root, audio.samples);
  timings["audioFeatures"] = seconds(started);
  started = Clock::now();
  DenoiseStats denoiseStats;
  auto raw = predict(root, features, cfg, seed, &denoiseStats);
  timings["motionSampling"] = seconds(started);
  started = Clock::now();
  auto driven = stitchMotion(root, raw, s, cfg, seed, still);
  timings["stitching"] = seconds(started);
  fs::path featurePath = out;
  featurePath += ".feature.f32";
  require(!fs::exists(featurePath), "feature output exists");
  save(featurePath, s.feature);
  return {{"version", 1},
          {"engine", "ditto-native-v1"},
          {"pipelineId", caps.at("pipelineId")},
          {"motionExecutionRevision", caps.at("motionExecutionRevision")},
          {"sourceSha256", av.at("sourceSha256")},
          {"audioFloat32Sha256",
           avatar::sha256(audio.samples.data(), audio.samples.size() * 4)},
          {"sampleCount", audio.samples.size()},
          {"fps", 25},
          {"seed", seed},
          {"motionControls", controls},
          {"source", sourceJson(s)},
          {"sourceConditioning", cfg.value("sourceConditioning", std::string{})},
          {"referenceConditions", cfg.at("default_kwargs").at("ch_info")},
          {"sourceMouthGeometry",cfg.value("sourceMouthGeometry",J())},
          {"mouthCalibrationRuntime",cfg.value("mouthCalibrationRuntime",J())},
          {"mouthReference", mouthReference},
          {"featurePath", fs::absolute(featurePath).string()},
          {"featureSha256", avatar::sha256(featurePath)},
          {"drivingPoints", driven},
          {"rawMotion", raw},
          {"denoiseConditionManifestSha256", denoiseStats.manifestSha256},
          {"denoiseSubtimingsSeconds", denoiseStats.json()},
          {"sourceKind", av.at("kind")},
          {"timingsSeconds", timings}};
}
} // namespace
J runtimeCheck() {
  (void)runtimeEnvironment();
  float value = 42;
  const int64_t shape[] = {1};
  auto tensor = Ort::Value::CreateTensor<float>(runtimeMemory(), &value, 1, shape, 1);
  require(tensor.IsTensor() && tensor.GetTensorData<float>()[0] == value, "runtime tensor check");
  return {{"runtime", OrtGetApiBase()->GetVersionString()},
          {"providers", Ort::GetAvailableProviders()}, {"tensorCheck", true},
          {"gpuInferenceValidated", false}};
}
J capabilities(const fs::path &models) {
  auto m = verify(models);
  const auto condition = conditionPackage(models);
  const auto identity = m.at("pipelineId").get<std::string>() + condition.sha256 + mouth_detail::runtimeRevision;
  return {{"engine", "ditto-native-v1"},
          {"execution", "local"},
          {"pipelineId", m.at("pipelineId")},
          {"denoiseConditionManifestSha256", condition.sha256},
          {"mouthReferenceRevision",mouth_detail::runtimeRevision},
          {"mouthReferenceCatalogSha256", fs::exists(models / "mouth-reference.json")
              ? avatar::sha256(models / "mouth-reference.json") : std::string{}},
          {"motionExecutionRevision", avatar::sha256(identity.data(), identity.size())},
          {"fps", 25},
          {"sampleRate", 16000},
          {"styles", J::array({"official"})},
          {"motionControlRevision", "emotion-and-relative-gains-v1"},
          {"emotionLabels", J::array({"angry", "disgust", "fear", "happy", "neutral", "sad", "surprise", "contempt"})},
          {"motionGainRange", J::array({0, 2})},
          {"runtime", "C++ ONNX Runtime"},
          {"manifestSha256", avatar::sha256(models / "manifest.json")}};
}
void prepareDriver(const fs::path &models, const fs::path &dir,
                   const fs::path &plan, const fs::path &output) {
  InferencePriority priority("driver-main");
  require(!fs::exists(output), "driver output exists");
  auto result = build(models, dir, plan, output);
  auto temp = output;
  temp += ".partial";
  avatar::writeJson(temp, result);
  avatar::checkCancelled();
  fs::rename(temp, output);
}
void renderTimeline(const fs::path &root, const fs::path &dir,
                    const fs::path &planPath, const fs::path &result) {
  InferencePriority priority("render-main");
  auto begin = std::chrono::steady_clock::now();
  auto caps = capabilities(root), plan = avatar::readJson(planPath),
       av = avatar::readJson(dir / "avatar.json");
  checkDriver(plan, caps);
  const bool still = av.at("kind") == "image";
  auto source = avatar::member(dir, av.at("source"));
  auto audio = avatar::readAudioPlan(planPath);
  require(!audio.samples.empty(), "empty audio");
  const size_t motionFrames = (audio.samples.size() + 639) / 640;
  int fps = plan.value("fpsNum", 25), den = plan.value("fpsDen", 1);
  require(fps > 0 && den > 0 && double(fps) / den <= 240, "fps");
  int64_t total = (int64_t(audio.samples.size()) * fps + 16000LL * den - 1) /
                  (16000LL * den),
          first = plan.value("firstFrame", int64_t(0)),
          count = plan.value("frameCount", total - first);
  require(first >= 0 && count > 0 && first <= total - count, "render range");
  VideoReader reader(source);
  Image image;
  require(reader.next(image), "source decode");
  bool lossless = !image.alpha.empty() || image.width % 2 || image.height % 2;
  auto output = result;
  output.replace_extension(lossless ? ".mkv" : ".mp4");
  auto tmp = output;
  tmp += ".partial";
  require(!fs::exists(output) && !fs::exists(tmp), "output exists");
  auto cfg = avatar::readJson(root / "config.json");
  const auto controls = control_detail::normalize(plan.value("audioDriver", J::object()));
  const int seed = plan.value(
      "seed", plan.value("audioDriver", J::object()).value("seed", 1024));
  const char *motionSerial = std::getenv("XINSHEN_DITTO_MOTION_SERIAL");
  const bool motionStreaming = still && !plan.contains("motionCache") &&
      !(motionSerial && std::string(motionSerial) == "1");
  J cache;
  Source s;
  V driven;
  // Destroy/join the stream before its referenced source, config and audio.
  std::unique_ptr<MotionStream> motion;
  if (motionStreaming) {
    require(avatar::sha256(source) == av.at("sourceSha256"), "source changed");
    J timings = {{"validationAndAudioPlan", seconds(begin)}};
    const auto prepStarted = Clock::now();
    {
      Prep prep(root, false);
      s = prep(image);
      sourceCondition(cfg, s, image, root);
    }
    control_detail::apply(cfg, controls);
    timings["sourcePreparation"] = seconds(prepStarted);
    const auto mouthReference = configureMouthReference(root, plan, caps, av, s, cfg, audio.samples);
    fs::path featurePath = result;
    featurePath += ".feature.f32";
    require(!fs::exists(featurePath), "feature output exists");
    save(featurePath, s.feature);
    cache = {{"version", 1}, {"engine", "ditto-native-v1"},
             {"pipelineId", caps.at("pipelineId")},
             {"motionExecutionRevision", caps.at("motionExecutionRevision")},
             {"sourceSha256", av.at("sourceSha256")},
             {"audioFloat32Sha256", avatar::sha256(audio.samples.data(), audio.samples.size() * 4)},
             {"sampleCount", audio.samples.size()}, {"fps", 25}, {"seed", seed},
             {"motionControls", controls},
             {"source", sourceJson(s)}, {"featurePath", fs::absolute(featurePath).string()},
             {"sourceConditioning", cfg.value("sourceConditioning", std::string{})},
             {"referenceConditions", cfg.at("default_kwargs").at("ch_info")},
             {"sourceMouthGeometry",cfg.value("sourceMouthGeometry",J())},
             {"mouthCalibrationRuntime",cfg.value("mouthCalibrationRuntime",J())},
             {"mouthReference", mouthReference},
             {"featureSha256", avatar::sha256(featurePath)}, {"sourceKind", av.at("kind")},
             {"timingsSeconds", timings}};
    motion = std::make_unique<MotionStream>(root, audio.samples, s, cfg, seed, begin);
    motion->start();
  } else {
    if (plan.contains("motionCache"))
      cache = avatar::readJson(plan.at("motionCache").get<std::string>());
    else
      cache = build(root, dir, planPath, result);
    require(control_detail::cacheMatches(cache, controls),
            "人物表情或动作幅度已变化，请重新预测动作");
    require(cache.at("engine") == "ditto-native-v1" &&
                cache.value("motionExecutionRevision", "") == caps.at("motionExecutionRevision") &&
                cache.at("pipelineId") == caps.at("pipelineId") &&
                cache.value("denoiseConditionManifestSha256", "") == caps.at("denoiseConditionManifestSha256") &&
                cache.at("sourceSha256") == avatar::sha256(source) &&
                cache.at("seed") == seed && cache.at("sampleCount") == audio.samples.size() &&
                cache.at("audioFloat32Sha256") ==
                    avatar::sha256(audio.samples.data(), audio.samples.size() * 4),
            "动作缓存与输入或模型不一致，请重新生成");
    auto featurePath = fs::path(cache.at("featurePath").get<std::string>());
    require(avatar::sha256(featurePath) == cache.at("featureSha256"), "feature checksum");
    s = sourceRead(cache.at("source"));
    if (!cfg.value("sourceConditioning", std::string{}).empty()) {
      require(cache.value("sourceConditioning", std::string{}) ==
                  cfg.at("sourceConditioning"),
              "动作缓存缺少源人物参考条件，请重新生成");
      cfg["default_kwargs"]["ch_info"] = cache.at("referenceConditions");
      cfg["sourceMouthGeometry"]=cache.value("sourceMouthGeometry",J());
      sourceNeutralEmotion(cfg);
    }
    control_detail::apply(cfg, controls);
    const auto mouthReference = configureMouthReference(root, plan, caps, av, s, cfg, audio.samples);
    require(mouth_detail::cacheMatches(cache, mouthReference),
            "口型适配策略已变化，请重新生成动作缓存");
    cache["mouthReference"] = mouthReference;
    s.feature = read(featurePath, 32 * 16 * 64 * 64);
    driven = cache.at("drivingPoints").get<V>();
    finite(driven);
    require(driven.size() == motionFrames * 63, "motion frame count");
  }
  auto xs = points(s.motion, s.kp);
  dump("source-transformed-kp", xs);
  auto framePoints = [&](size_t frame) {
    return motion ? motion->pointsAt(frame)
                  : V(driven.begin() + frame * 63, driven.begin() + (frame + 1) * 63);
  };
  auto check = [&] {
    avatar::checkCancelled();
    if (motion) motion->check();
  };
  auto guarded = [&](auto operation) {
    try { return operation(); }
    catch (...) {
      if (motion) motion->stop(std::current_exception());
      throw;
    }
  };
  auto writeNewJson = [](const fs::path &path, const J &value) {
    auto partial = fs::path(path.string() + ".partial");
    require(!fs::exists(path) && !fs::exists(partial), "JSON output exists");
    try {
      avatar::writeJson(path, value);
    } catch (...) {
      std::error_code ignored;
      fs::remove(partial, ignored);
      throw;
    }
  };
  try {
    std::unique_ptr<Prep> videoPrep;
    std::unique_ptr<Net> videoStitch;
    V raw;
    float videoRatio = 1;
    int64_t lastSource = -1;
    if (!still) {
      videoPrep = std::make_unique<Prep>(root);
      videoStitch = std::make_unique<Net>(root / "stitch.onnx", false);
      raw = cache.at("rawMotion").get<V>();
      require(raw.size() == driven.size() / 63 * 265, "raw video motion count");
      finite(raw);
      auto ref = motionFromJson(
          cfg.at("default_kwargs").at("ch_info").at("x_s_info_lst").at(0));
      videoRatio = (ref[0] + 1) / (s.motion[0] + 1);
    }
    Net generator(root / "generator.onnx");
    bool includeAudio = plan.value("includeAudio", false);
    require(still || !plan.contains("segmentDirectory"), "segments require a still-image source");
    RenderSegments writer(plan, total, first, count, output, tmp, audio.samples,
                          image.width, image.height, fps, den, lossless);
    count = writer.count();
    first = writer.frameAt(0);
    CompositeCache compositeCache;
    double inferenceSeconds = 0, compositeSeconds = 0, encodeSeconds = 0;
    double warpSeconds = 0, decodeSeconds = 0, motionWaitSeconds = 0;
    double firstFrameSeconds = 0, firstSegmentSeconds = 0;
    std::string renderProfile = "onnx", renderManifestSha256;
#if defined(__APPLE__)
    if (generator.native) {
      renderProfile = generator.native->profile();
      renderManifestSha256 = generator.native->manifestSha256();
    }
#endif
    const double setupSeconds = seconds(begin);
    const char *serial = std::getenv("XINSHEN_DITTO_RENDER_SERIAL");
    const bool pipelined = still && !(serial && std::string(serial) == "1");
    if (pipelined) {
      // Reuse the native bounded FIFO: GPU inference owns its session, while
      // one worker owns floating-point pasteback and encoding. No frame drops.
      auto infer = [&](int64_t i) {
        return guarded([&]() -> V {
        const int64_t a = i * 16000LL * den / fps;
        const int64_t b = std::min<int64_t>(
            audio.covered.size(), ((i + 1) * 16000LL * den + fps - 1) / fps);
        if (a >= b ||
            !std::any_of(audio.covered.begin() + a, audio.covered.begin() + b,
                         [](uint8_t x) { return x != 0; }))
          return V{};
        const size_t mi =
            std::min<size_t>(motionFrames - 1, i * 25LL * den / fps);
        V xd;
        { AccumulateTime timer{motionWaitSeconds}; xd = framePoints(mi); }
        AccumulateTime timer{inferenceSeconds};
        auto face =
            generator.run({s.feature, xd, xs},
                          {{1, 32, 16, 64, 64}, {1, 21, 3}, {1, 21, 3}})[0];
        if (i == first)
          dump("face-first", face);
        return face;
        });
      };
      auto encode = [&](size_t index, V face) {
        guarded([&] {
        InferencePriority priority("encode", index == 0);
        Image frame;
        {
          AccumulateTime timer{compositeSeconds};
          frame =
              face.empty() ? image : composite(image, s, face, compositeCache);
        }
        {
          AccumulateTime timer{encodeSeconds};
          writer.write(frame);
        }
        if (!index) firstFrameSeconds = seconds(begin);
        if (!firstSegmentSeconds && !writer.records().empty()) firstSegmentSeconds = seconds(begin);
        avatar::progress("render", index + 1, count);
        });
      };
#if defined(__APPLE__)
      if (generator.native && generator.native->split()) {
        struct WarpedFrame {
          int64_t index;
          lp::CoreMLGenerator::PreparedPtr features;
        };
        // Independent GPU/Neural Engine owners overlap adjacent frames.
        // The bounded FIFO retains each CoreML output until decoding finishes.
        runFramePipeline<WarpedFrame, V>(
            count,
            [&](size_t index) {
              InferencePriority priority("prepare", index == 0);
              return guarded([&] {
              const auto i = writer.frameAt(index);
              const int64_t a = i * 16000LL * den / fps;
              const int64_t b = std::min<int64_t>(
                  audio.covered.size(), ((i + 1) * 16000LL * den + fps - 1) / fps);
              if (a >= b || !std::any_of(audio.covered.begin() + a,
                                        audio.covered.begin() + b,
                                        [](uint8_t x) { return x != 0; }))
                return WarpedFrame{i, {}};
              const size_t mi = std::min<size_t>(
                  motionFrames - 1, i * 25LL * den / fps);
              V xd, maskDriving;
              {
                AccumulateTime timer{motionWaitSeconds};
                xd = framePoints(mi);
                const auto anchor = generator.native->maskAnchor(i);
                if (anchor != i)
                  maskDriving = framePoints(std::min<size_t>(
                      motionFrames - 1, anchor * 25LL * den / fps));
              }
              AccumulateTime timer{warpSeconds};
              return WarpedFrame{i, generator.native->prepareRefs(
                  generator.inputs, {&s.feature, &xd, &xs},
                  {{1, 32, 16, 64, 64}, {1, 21, 3}, {1, 21, 3}}, i, maskDriving)};
              });
            },
            [&](WarpedFrame frame) {
              return guarded([&]() -> V {
              if (!frame.features) return V{};
              AccumulateTime timer{decodeSeconds};
              auto face = generator.native->finish(std::move(frame.features));
              finite(face);
              ++generator.runs;
              if (frame.index == first) dump("face-first", face);
              return face;
              });
            }, encode, check);
        inferenceSeconds = warpSeconds + decodeSeconds;
        generator.runSeconds = inferenceSeconds;
      } else
#endif
        runFramePipeline<int64_t, V>(
            count, [&](size_t index) { return writer.frameAt(index); }, infer,
            encode, check);
    } else
      for (int64_t index = 0; index < count; ++index) {
        const int64_t i = writer.frameAt(index);
        check();
        size_t mi =
            std::min<size_t>(motionFrames - 1, i * 25LL * den / fps);
        V xd;
        { AccumulateTime timer{motionWaitSeconds}; xd = framePoints(mi); }
        int64_t a = i * 16000LL * den / fps,
                b = std::min<int64_t>(audio.covered.size(),
                                      ((i + 1) * 16000LL * den + fps - 1) /
                                          fps);
        bool covered =
            a < b && std::any_of(
                         audio.covered.begin() + a, audio.covered.begin() + b,
                         [](uint8_t x) { return x != 0; });
        if (!still) {
          const int64_t offset =
              plan.value("sourceOffsetTicks", int64_t(0)) * 1000000 / 120000;
          const int64_t pts = i * 1000000LL * den / fps + offset;
          require(pts >= 0 && pts < av.at("durationUs").get<int64_t>(),
                  "口播源范围超出视频时长");
          require(reader.readAt(pts, image), "video decode");
          if (covered) {
            if (image.ptsUs != lastSource) {
              s = (*videoPrep)(image);
              lastSource = image.ptsUs;
              xs = points(s.motion, s.kp);
            }
            xd = stitchMotion(root, raw, s, cfg, cache.value("seed", 1024),
                              false, mi, videoStitch.get(), videoRatio);
          }
        }
        if (covered) {
          auto stage = Clock::now();
          auto face =
              generator.run({s.feature, xd, xs},
                            {{1, 32, 16, 64, 64}, {1, 21, 3}, {1, 21, 3}})[0];
          if (i == first)
            dump("face-first", face);
          inferenceSeconds += seconds(stage);
          stage = Clock::now();
          auto frame = composite(image, s, face, compositeCache);
          compositeSeconds += seconds(stage);
          stage = Clock::now();
          writer.write(frame);
          encodeSeconds += seconds(stage);
        } else {
          AccumulateTime timer{encodeSeconds};
          writer.write(image);
        }
        if (i == first) firstFrameSeconds = seconds(begin);
        if (!firstSegmentSeconds && !writer.records().empty()) firstSegmentSeconds = seconds(begin);
        avatar::progress("render", index + 1, count);
      }
    {
      AccumulateTime timer{encodeSeconds};
      writer.finish();
    }
    if (motion) {
      motion->complete();
      motion->store(cache);
    }
    check();
    std::string generatedMotionCache;
    if (!plan.contains("motionCache") || plan.contains("driverOutput")) {
      auto driverPath = plan.contains("driverOutput")
          ? fs::path(plan.at("driverOutput").get<std::string>())
          : fs::path(result.string() + ".motion.json");
      writeNewJson(driverPath, cache);
      check();
      generatedMotionCache = fs::absolute(driverPath).string();
    }
    writer.publish();
    writeNewJson(
        result,
        {{"path", writer.path().string()},
         {"segmented", writer.segmented()},
         {"segments", writer.records()},
         {"firstSegmentSeconds", firstSegmentSeconds},
         {"engine", "ditto-native-v1"},
         {"execution", "local"},
         {"frames", count},
         {"width", image.width},
         {"height", image.height},
         {"fpsNum", fps},
         {"fpsDen", den},
         {"hasAudio", includeAudio},
         {"pipelineId", caps.at("pipelineId")},
         {"renderProfile", renderProfile},
         {"renderManifestSha256", renderManifestSha256},
         {"motionCacheReused", plan.contains("motionCache")},
         {"motionControls", controls},
         {"emotionCondition", controls.at("emo").get<int>() < 0 ? J(nullptr) : cfg.at("default_kwargs").at("emo")},
         {"mouthReference", cache.at("mouthReference")},
         {"mouthCalibrationRuntime",cache.value("mouthCalibrationRuntime",J())},
         {"motionStreaming", motionStreaming},
         {"streamingAudioCpuWorkers", cache.value("streamingAudioCpuWorkers", J(nullptr))},
         {"streamingMotionCpuWorkers", cache.value("streamingMotionCpuWorkers", J(nullptr))},
         {"streamingCpuWorkersPerStage", cache.value("streamingCpuWorkersPerStage", J(nullptr))},
         {"inferencePriority", inferencePriorityStats()},
         {"generatedMotionCache", generatedMotionCache},
         {"motionTimingsSeconds", cache.value("timingsSeconds", J::object())},
         {"motionStreamingWallSeconds", cache.value("streamingWallSeconds", J::object())},
         {"denoiseConditionManifestSha256", cache.value("denoiseConditionManifestSha256", "")},
         {"motionExecutionRevision", caps.at("motionExecutionRevision")},
         {"denoiseSubtimingsSeconds", cache.value("denoiseSubtimingsSeconds", J::object())},
         {"sourceSha256", cache.at("sourceSha256")},
         {"audioFloat32Sha256", cache.at("audioFloat32Sha256")},
         {"seed", cache.at("seed")},
         {"pipelined", pipelined},
         {"timingsOverlap", pipelined || motionStreaming},
         {"firstFrameSeconds", firstFrameSeconds},
         {"timingsSeconds",
          {{"setup", setupSeconds},
           {"generator", inferenceSeconds},
           {"generatorWarp", warpSeconds},
           {"generatorDecode", decodeSeconds},
           {"motionWait", motionWaitSeconds},
           {"composite", compositeSeconds},
           {"encodeAndMux", encodeSeconds}}},
         {"elapsedSeconds", std::chrono::duration<double>(
                                std::chrono::steady_clock::now() - begin)
                                .count()}});
  } catch (...) {
    if (motion) motion->stop();
    std::error_code ec;
    fs::remove(tmp, ec);
    throw;
  }
}
void validateDenoiser(const fs::path &root, const fs::path &fixture,
                      const fs::path &output) {
  auto start = std::chrono::steady_clock::now();
  Net net(root / "denoise.onnx", false);
  auto x = read(fixture / "x.f32", 80 * 265),
       kp = read(fixture / "cond_frame.f32", 265),
       cond = read(fixture / "cond.f32", 80 * 1103);
  auto loaded = std::chrono::steady_clock::now();
  auto ys = net.run({x, kp, cond, V(1)},
                    {{1, 80, 265}, {1, 265}, {1, 80, 1103}, {1}}, 999);
  J errors;
  for (int i = 0; i < 2; ++i) {
    std::string name = i ? "x_start" : "pred_noise";
    auto expected = read(fixture / (name + ".f32"), 80 * 265);
    double sum = 0;
    float maximum = 0;
    for (size_t j = 0; j < expected.size(); ++j) {
      float e = std::abs(expected[j] - ys[i][j]);
      maximum = std::max(maximum, e);
      sum += e;
    }
    errors[name] = {{"maxAbs", maximum}, {"meanAbs", sum / expected.size()}};
  }
  errors["loadSeconds"] = std::chrono::duration<double>(loaded - start).count();
  errors["runSeconds"] =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - loaded)
          .count();
  avatar::writeJson(output, errors);
  std::cout << errors.dump() << '\n';
}
void validateMotion(const fs::path &root, const fs::path &fixture,
                    const fs::path &result) {
  auto start = std::chrono::steady_clock::now();
  auto cfg = avatar::readJson(root / "config.json");
  auto features =
      read(fixture / "hubert.f32", fs::file_size(fixture / "hubert.f32") / 4);
  auto raw = predict(root, features, cfg, 1024);
  auto ref = avatar::readJson(fixture / "source.json");
  Source source;
  source.motion = motionFromJson(ref.at("motion"));
  source.kp = flatten(ref.at("motion").at("kp"));
  auto p = stitchMotion(root, raw, source, cfg, 1024);
  auto expected = read(fixture / "points.f32", p.size());
  double sum = 0;
  float mx = 0;
  for (size_t i = 0; i < p.size(); ++i) {
    float e = std::abs(p[i] - expected[i]);
    sum += e;
    mx = std::max(mx, e);
  }
  avatar::writeJson(
      result, {{"frames", p.size() / 63},
               {"pointMaxAbs", mx},
               {"pointMeanAbs", sum / p.size()},
               {"elapsedSeconds", std::chrono::duration<double>(
                                      std::chrono::steady_clock::now() - start)
                                      .count()}});
}
} // namespace ditto
