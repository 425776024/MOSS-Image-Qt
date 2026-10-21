#include "coreml.h"
#include "../avatar_runtime.h"
#import <CoreML/CoreML.h>
#import <Foundation/Foundation.h>
#include <chrono>
#include <cstring>
#include <iostream>
#include <set>

namespace lp {
namespace fs = std::filesystem;
namespace {
using Clock = std::chrono::steady_clock;
struct Timing {
  double inputMs = 0, predictMs = 0, readbackMs = 0;
  size_t runs = 0;
};
NSURL* url(const fs::path& path) { return [NSURL fileURLWithPath:@(path.c_str())]; }
void fail(NSError* error, const char* operation) {
  throw std::runtime_error(std::string(operation) + ": " +
      (error ? error.localizedDescription.UTF8String : "invalid model contract"));
}
void require(bool ok, const char* operation) {
  if (!ok) fail(nil, operation);
}
double milliseconds(Clock::time_point first, Clock::time_point last) {
  return std::chrono::duration<double, std::milli>(last - first).count();
}
void tensorContract(MLFeatureDescription* feature, NSArray<NSNumber*>* shape,
                    const char* operation) {
  auto contract = feature.multiArrayConstraint;
  require(contract && contract.dataType == MLMultiArrayDataTypeFloat32 &&
              [contract.shape isEqualToArray:shape], operation);
}
void sourceContract(MLModel* model, size_t count = 3) {
  auto inputs = model.modelDescription.inputDescriptionsByName;
  require(inputs.count == count, "CoreML generator input count");
  tensorContract(inputs[@"feature_3d"], @[@1, @32, @16, @64, @64], "CoreML feature input");
  tensorContract(inputs[@"kp_driving"], @[@1, @21, @3], "CoreML driving input");
  tensorContract(inputs[@"kp_source"], @[@1, @21, @3], "CoreML source input");
}
NSString* const maskName = @"_w_dense_motion_network_Softmax_output_0";
NSString* const occlusionName = @"_w_dense_motion_network_Sigmoid_output_0";
void maskContract(NSDictionary<NSString*, MLFeatureDescription*>* features) {
  tensorContract(features[maskName], @[@1, @22, @16, @64, @64], "Ditto temporal mask contract");
  tensorContract(features[occlusionName], @[@1, @1, @64, @64], "Ditto temporal occlusion contract");
}
CoreMLGenerator::InputRefs inputRefs(const std::vector<std::vector<float>>& data) {
  CoreMLGenerator::InputRefs result;
  result.reserve(data.size());
  for (const auto& value : data) result.push_back(&value);
  return result;
}
NSString* faceContract(MLModel* model) {
  auto outputs = model.modelDescription.outputDescriptionsByName;
  require(outputs.count == 1, "CoreML generator output count");
  NSString* name = outputs.allKeys.firstObject;
  tensorContract(outputs[name], @[@1, @3, @512, @512], "CoreML generator output");
  return name;
}
void verifyFiles(const fs::path& directory, const avatar::Json& manifest,
                 const std::set<std::string>& packages) {
  const auto& files = manifest.at("files");
  require(files.is_array() && files.size() >= packages.size() * 3,
          "CoreML package file manifest");
  std::set<std::string> declared;
  for (const auto& item : files) {
    const fs::path relative = item.at("file").get<std::string>();
    require(!relative.empty() && !relative.is_absolute(), "CoreML package path");
    for (const auto& component : relative)
      require(component != ".." && component != ".", "CoreML package path traversal");
    require(packages.count(relative.begin()->string()) &&
                declared.insert(relative.generic_string()).second,
            "CoreML package path or duplicate");
    const auto path = directory / relative;
    require(fs::is_regular_file(path) && !fs::is_symlink(path) &&
                fs::file_size(path) == item.at("bytes").get<uintmax_t>() &&
                avatar::sha256(path) == item.at("sha256"),
            "CoreML package checksum; rebuild the application");
  }
  std::set<std::string> actual;
  for (const auto& package : packages) {
    const auto root = directory / package;
    require(fs::is_directory(root) && !fs::is_symlink(root), "CoreML package missing");
    for (const auto& item : fs::recursive_directory_iterator(root)) {
      require(!item.is_symlink(), "CoreML package symlink");
      if (item.is_regular_file())
        actual.insert(item.path().lexically_relative(directory).generic_string());
    }
  }
  require(actual == declared, "CoreML package files differ from manifest");
}
struct RuntimeOptimization {
  bool configured = false;
  bool fastPrediction = false;
  bool infrequentReshape = false;
};
RuntimeOptimization runtimeOptimization(const avatar::Json& manifest) {
  if (!manifest.contains("runtimeOptimization")) return {};
  const auto& options = manifest.at("runtimeOptimization");
  require(options.is_object() && options.size() == 2 &&
              options.contains("specializationStrategy") && options.contains("reshapeFrequency") &&
              options.at("specializationStrategy").is_string() && options.at("reshapeFrequency").is_string(),
          "CoreML runtimeOptimization requires specializationStrategy and reshapeFrequency");
  const auto specialization = options.at("specializationStrategy").get<std::string>();
  const auto reshape = options.at("reshapeFrequency").get<std::string>();
  require(specialization == "default" || specialization == "fastPrediction",
          "CoreML runtimeOptimization specializationStrategy");
  require(reshape == "frequent" || reshape == "infrequent",
          "CoreML runtimeOptimization reshapeFrequency");
  if (@available(macOS 15.0, *)) {
    return {true, specialization == "fastPrediction", reshape == "infrequent"};
  }
  fail(nil, "CoreML runtimeOptimization requires macOS 15.0");
  return {};
}
MLModel* loadModel(const fs::path& directory, const fs::path& cache,
                   const char* basename, MLComputeUnits units, const RuntimeOptimization& optimization) {
  const auto compiled = cache / (std::string(basename) + ".mlmodelc");
  NSError* error = nil;
  if (!fs::exists(compiled)) {
    NSURL* temporary = [MLModel compileModelAtURL:url(directory / (std::string(basename) + ".mlpackage"))
                                            error:&error];
    if (!temporary) fail(error, "Compile CoreML generator");
    if (![[NSFileManager defaultManager] moveItemAtURL:temporary toURL:url(compiled) error:&error] &&
        !fs::exists(compiled)) fail(error, "Cache CoreML generator");
  }
  MLModelConfiguration* config = [MLModelConfiguration new];
  config.computeUnits = units;
  config.allowLowPrecisionAccumulationOnGPU = NO;
  if (optimization.configured) {
    if (@available(macOS 15.0, *)) {
      MLOptimizationHints* hints = [MLOptimizationHints new];
      hints.specializationStrategy = optimization.fastPrediction ?
          MLSpecializationStrategyFastPrediction : MLSpecializationStrategyDefault;
      hints.reshapeFrequency = optimization.infrequentReshape ?
          MLReshapeFrequencyHintInfrequent : MLReshapeFrequencyHintFrequent;
      config.optimizationHints = hints;
    } else fail(nil, "CoreML runtimeOptimization requires macOS 15.0");
  }
  auto model = [MLModel modelWithContentsOfURL:url(compiled) configuration:config error:&error];
  if (!model) fail(error, "Load CoreML generator");
  return model;
}
id<MLFeatureProvider> sourceProvider(MLModel* model,
    NSMutableDictionary<NSString*, MLFeatureValue*>* inputs,
    const std::vector<std::string>& names, const CoreMLGenerator::InputRefs& data,
    const std::vector<std::vector<int64_t>>& shapes, bool* sourceChanged = nullptr) {
  require(names.size() == 3 && data.size() == names.size() && shapes.size() == names.size(),
          "CoreML generator input arrays");
  if (sourceChanged) *sourceChanged = false;
  std::set<std::string> seen;
  NSError* error = nil;
  for (size_t i = 0; i < names.size(); ++i) {
    require(seen.insert(names[i]).second, "CoreML generator duplicate input");
    require(data[i] != nullptr, "CoreML generator null input");
    NSString* name = @(names[i].c_str());
    auto contract = model.modelDescription.inputDescriptionsByName[name].multiArrayConstraint;
    NSMutableArray<NSNumber*>* shape = [NSMutableArray new];
    for (auto dimension : shapes[i]) [shape addObject:@(dimension)];
    require(contract && [contract.shape isEqualToArray:shape] &&
                contract.dataType == MLMultiArrayDataTypeFloat32, "CoreML generator input shape/type");
    MLMultiArray* array = inputs[name].multiArrayValue;
    const bool fresh = array == nil;
    if (fresh) {
      array = [[MLMultiArray alloc] initWithShape:shape dataType:MLMultiArrayDataTypeFloat32 error:&error];
      if (!array) fail(error, "Allocate CoreML tensor");
    }
    require(array.count == data[i]->size(), "CoreML generator input size");
    const bool sourceInput = names[i] == "feature_3d" || names[i] == "kp_source";
    const size_t bytes = data[i]->size() * sizeof(float);
    // The owned tensor is the content snapshot: pointer identity alone cannot
    // detect callers that mutate a vector in place between frames.
    const bool unchanged = sourceInput && !fresh &&
        std::memcmp(array.dataPointer, data[i]->data(), bytes) == 0;
    if (!unchanged) std::memcpy(array.dataPointer, data[i]->data(), bytes);
    if (sourceChanged && sourceInput && !unchanged) *sourceChanged = true;
    // Publish a newly allocated tensor only after its contents are initialized.
    if (fresh) inputs[name] = [MLFeatureValue featureValueWithMultiArray:array];
  }
  auto provider = [[MLDictionaryFeatureProvider alloc] initWithDictionary:inputs error:&error];
  if (!provider) fail(error, "CoreML generator inputs");
  return provider;
}
std::vector<float> faceValues(id<MLFeatureProvider> output, NSString* name) {
  MLMultiArray* array = [output featureValueForName:name].multiArrayValue;
  require(array && array.dataType == MLMultiArrayDataTypeFloat32 &&
              [array.shape isEqualToArray:@[@1, @3, @512, @512]], "CoreML generator output shape/type");
  std::vector<float> result(array.count);
  const float* source = static_cast<const float*>(array.dataPointer);
  // CoreML outputs may have padded strides; never assume compact storage.
  const auto strides = array.strides;
  const size_t sc = strides[1].unsignedLongValue, sy = strides[2].unsignedLongValue;
  const size_t sx = strides[3].unsignedLongValue;
  if (sx == 1 && sy == 512 && sc == 512 * 512) {
    std::memcpy(result.data(), source, result.size() * sizeof(float));
    return result;
  }
  for (size_t c = 0; c < 3; ++c) for (size_t y = 0; y < 512; ++y)
    for (size_t x = 0; x < 512; ++x)
      result[(c * 512 + y) * 512 + x] = source[c * sc + y * sy + x * sx];
  return result;
}
}
struct CoreMLGenerator::State {
  id activity = nil;
  MLModel* model = nil;
  MLModel* motionMask = nil;
  MLModel* decoder = nil;
  id<MLFeatureProvider> cachedMask = nil;
  MLMultiArray* anchorDriving = nil;
  int64_t lastFrame = -2;
  int maskStride = 1;
  size_t maskRuns = 0;
  double maskMs = 0, applyMs = 0;
  NSMutableDictionary<NSString*, MLFeatureValue*>* inputs = nil;
  NSString* outputName = nil;
  std::string profile, manifestSha256;
  // The producer exclusively writes sourceTiming; the consumer exclusively
  // writes decoderTiming. The destructor runs after the pipeline joins.
  Timing sourceTiming, decoderTiming;
  ~State() {
    if (activity) [[NSProcessInfo processInfo] endActivity:activity];
  }
};
class CoreMLGenerator::Prepared {
  friend class CoreMLGenerator;
  id<MLFeatureProvider> output_ = nil;
  const State* owner_ = nullptr;
};
CoreMLGenerator::CoreMLGenerator(std::unique_ptr<State> state) : state_(std::move(state)) {}
CoreMLGenerator::~CoreMLGenerator() {
  if (std::getenv("XINSHEN_INFERENCE_PROFILE")) {
    const auto& a = state_->sourceTiming;
    const auto& b = state_->decoderTiming;
    std::cerr << "[liveportrait_coreml] runs=" << (split() ? b.runs : a.runs)
              << " input_ms=" << a.inputMs << " predict_ms=" << a.predictMs + b.predictMs
              << " readback_ms=" << a.readbackMs + b.readbackMs
              << " profile=" << state_->profile << '\n';
    if (split())
      std::cerr << "[ditto_coreml_split] warp_runs=" << a.runs << " warp_ms=" << a.predictMs
                << " decoder_runs=" << b.runs << " decoder_ms=" << b.predictMs
                << " bridge_ms=" << a.readbackMs << " readback_ms=" << b.readbackMs << '\n';
    if (state_->motionMask)
      std::cerr << "[ditto_coreml_temporal] mask_stride=" << state_->maskStride
                << " mask_runs=" << state_->maskRuns << " mask_ms=" << state_->maskMs
                << " apply_runs=" << a.runs << " apply_ms=" << state_->applyMs << '\n';
  }
}
bool CoreMLGenerator::split() const { return state_->decoder != nil; }
const std::string& CoreMLGenerator::profile() const { return state_->profile; }
const std::string& CoreMLGenerator::manifestSha256() const { return state_->manifestSha256; }
int64_t CoreMLGenerator::maskAnchor(int64_t frameIndex) const {
  return state_->motionMask && frameIndex >= 0 ?
      frameIndex - frameIndex % state_->maskStride : frameIndex;
}
std::unique_ptr<CoreMLGenerator> CoreMLGenerator::create(const fs::path& source) {
  const char* device = std::getenv("XINSHEN_ONNX_DEVICE");
  const char* precision = std::getenv("XINSHEN_LP_PRECISION");
  if ((device && std::string(device) == "cpu") ||
      (precision && std::string(precision) == "fp32") ||
      (source.filename() != "generator_masked.onnx" && source.filename() != "generator.onnx")) return nullptr;
  if (@available(macOS 14.0, *)) {
    @autoreleasepool {
      fs::path directory = source.parent_path() / "coreml-fp16";
      bool accelerated = false;
      bool explicitCandidate = false;
      if (@available(macOS 15.0, *)) {
        // This override is scoped to Ditto; legacy LivePortrait keeps its path.
        if (source.filename() == "generator.onnx") {
          const char* candidate = std::getenv("XINSHEN_DITTO_COREML_CANDIDATE");
          explicitCandidate = candidate && *candidate;
          // The default preserves full spatial resolution and predicts a fresh
          // warp every frame. Reduced/temporal packages require an explicit opt-in.
          auto selected = explicitCandidate ? fs::path(candidate) :
              source.parent_path() / "coreml-accelerated";
          if (explicitCandidate)
            require(fs::is_regular_file(selected / "manifest.json"), "Ditto candidate manifest missing");
          if (fs::exists(selected / "manifest.json")) {
            directory = selected;
            accelerated = true;
          }
        }
      }
      if (!fs::exists(directory / "manifest.json")) return nullptr;
      const auto manifest = avatar::readJson(directory / "manifest.json");
      require(manifest.at("sourceSha256") == avatar::sha256(source), "CoreML canonical model checksum");
      const auto optimization = runtimeOptimization(manifest);
      auto state = std::make_unique<State>();
      const char* qos = std::getenv("XINSHEN_DITTO_QOS_USER_INITIATED");
      if (source.filename() == "generator.onnx" && (!qos || std::string(qos) == "1"))
        state->activity = [[NSProcessInfo processInfo]
            beginActivityWithOptions:NSActivityUserInitiatedAllowingIdleSystemSleep
            reason:@"Render the requested local Ditto video"];
      bool decoderGPU = false;
      if (accelerated) {
        const bool temporal = manifest.at("version") == 3;
        require((manifest.at("version") == 2 || temporal) && manifest.at("minimumMacOS") == "15.0",
                "Ditto accelerated manifest version/platform");
        if (!explicitCandidate)
          require(manifest.at("version") == 2 && manifest.at("spatialChange") == false &&
                      manifest.at("temporalSkipping") == false && manifest.at("decoderFeatureSize") == 64 &&
                      manifest.at("faceOutputSize") == 512,
                  "Ditto default requires a full-resolution per-frame v2 model");
        state->profile = manifest.at("profile").get<std::string>();
        require(temporal ? state->profile == "mask_reuse_cpu_gpu_decoder_cpu_ne" :
                    (state->profile == "warp_cpu_gpu_decoder_cpu_ne" ||
                     state->profile == "warp_cpu_gpu_decoder_cpu_gpu"), "Ditto accelerated execution profile");
        decoderGPU = state->profile == "warp_cpu_gpu_decoder_cpu_gpu";
        const auto& precisionSpec = manifest.at("precision");
        require(precisionSpec.at("warp") == "conv_fp16" &&
                    (precisionSpec.at("decoder") == "fp16" || precisionSpec.at("decoder") == "conv_fp16"),
                "Ditto accelerated precision profile");
        if (temporal) {
          const auto& reuse = manifest.at("motionReuse");
          require(reuse.at("stride").is_number_integer(), "Ditto temporal stride integer");
          state->maskStride = reuse.at("stride").get<int>();
          require(reuse.at("mode") == "hold" && state->maskStride >= 1 && state->maskStride <= 3 &&
                      reuse.at("freshKeypointsEveryFrame") == true &&
                      reuse.at("alignment") == "absolute_frame",
                  "Ditto temporal mask reuse contract");
          verifyFiles(directory, manifest, {"motion-mask.mlpackage", "warp-apply.mlpackage", "decoder.mlpackage"});
        } else verifyFiles(directory, manifest, {"warp.mlpackage", "decoder.mlpackage"});
      } else {
        require(manifest.at("version") == 1 && manifest.at("precision") == "conv_fp16",
                "LivePortrait FP16 model manifest");
        state->profile = "conv_fp16_cpu_gpu";
        verifyFiles(directory, manifest, {"generator.mlpackage"});
      }
      state->manifestSha256 = avatar::sha256(directory / "manifest.json");
      const char* configured = std::getenv("XINSHEN_COREML_CACHE");
      auto cache = configured ? fs::path(configured) : fs::temp_directory_path() / "xinshen-coreml-cache";
      cache /= "lp-native-" + state->manifestSha256;
      fs::create_directories(cache);
      if (accelerated && manifest.at("version") == 3) {
        state->motionMask = loadModel(directory, cache, "motion-mask", MLComputeUnitsCPUAndGPU, optimization);
        sourceContract(state->motionMask);
        auto outputs = state->motionMask.modelDescription.outputDescriptionsByName;
        require(outputs.count == 2, "Ditto temporal mask outputs");
        maskContract(outputs);
        state->model = loadModel(directory, cache, "warp-apply", MLComputeUnitsCPUAndGPU, optimization);
        sourceContract(state->model, 5);
        maskContract(state->model.modelDescription.inputDescriptionsByName);
      } else {
        state->model = loadModel(directory, cache, accelerated ? "warp" : "generator", MLComputeUnitsCPUAndGPU, optimization);
        sourceContract(state->model);
      }
      if (accelerated) {
        if (@available(macOS 15.0, *)) {
          state->decoder = loadModel(directory, cache, "decoder",
              decoderGPU ? MLComputeUnitsCPUAndGPU : MLComputeUnitsCPUAndNeuralEngine, optimization);
        }
        auto bridge = state->model.modelDescription.outputDescriptionsByName;
        require(bridge.count == 1, "Ditto warp output count");
        tensorContract(bridge[@"_w_Mul_output_0"], @[@1, @256, @64, @64], "Ditto warp bridge");
        auto inputs = state->decoder.modelDescription.inputDescriptionsByName;
        require(inputs.count == 1, "Ditto decoder input count");
        tensorContract(inputs[@"_w_Mul_output_0"], @[@1, @256, @64, @64], "Ditto decoder bridge");
        state->outputName = faceContract(state->decoder);
      } else state->outputName = faceContract(state->model);
      state->inputs = [NSMutableDictionary new];
      std::cerr << "[inference] " << source.filename() << " native CoreML " << state->profile
                << " manifest=" << state->manifestSha256 << '\n';
      if (optimization.configured)
        std::cerr << "[inference] CoreML runtimeOptimization specializationStrategy="
                  << (optimization.fastPrediction ? "fastPrediction" : "default")
                  << " reshapeFrequency=" << (optimization.infrequentReshape ? "infrequent" : "frequent") << '\n';
      return std::unique_ptr<CoreMLGenerator>(new CoreMLGenerator(std::move(state)));
    }
  }
  return nullptr;
}
CoreMLGenerator::PreparedPtr CoreMLGenerator::prepare(const std::vector<std::string>& names,
    const std::vector<std::vector<float>>& data, const std::vector<std::vector<int64_t>>& shapes,
    int64_t frameIndex, const std::vector<float>& maskDriving) {
  return prepareRefs(names, inputRefs(data), shapes, frameIndex, maskDriving);
}
CoreMLGenerator::PreparedPtr CoreMLGenerator::prepareRefs(const std::vector<std::string>& names,
    const InputRefs& data, const std::vector<std::vector<int64_t>>& shapes,
    int64_t frameIndex, const std::vector<float>& maskDriving) {
  require(split(), "Ditto prepare requires split generator");
  try {
  @autoreleasepool {
    const auto started = Clock::now();
    bool sourceChanged = false;
    auto provider = sourceProvider(state_->motionMask ?: state_->model, state_->inputs, names, data, shapes,
                                   &sourceChanged);
    // Only a consecutive sequence of the identical source may reuse a mask.
    // An unspecified index, seek, segment reorder, or source change refreshes it.
    const bool refresh = state_->motionMask &&
        (!state_->cachedMask || frameIndex < 0 || frameIndex != state_->lastFrame + 1 ||
         frameIndex % state_->maskStride == 0 || sourceChanged);
    const auto prepared = Clock::now();
    NSError* error = nil;
    if (state_->motionMask) {
      if (refresh) {
        id<MLFeatureProvider> maskProvider = provider;
        if (maskAnchor(frameIndex) != frameIndex) {
          require(maskDriving.size() == 63, "Ditto temporal anchor driving points");
          if (!state_->anchorDriving) {
            state_->anchorDriving = [[MLMultiArray alloc] initWithShape:@[@1, @21, @3]
                dataType:MLMultiArrayDataTypeFloat32 error:&error];
            if (!state_->anchorDriving) fail(error, "Allocate Ditto anchor keypoints");
          }
          std::memcpy(state_->anchorDriving.dataPointer, maskDriving.data(), 63 * sizeof(float));
          auto anchorInputs = [state_->inputs mutableCopy];
          anchorInputs[@"kp_driving"] = [MLFeatureValue featureValueWithMultiArray:state_->anchorDriving];
          maskProvider = [[MLDictionaryFeatureProvider alloc] initWithDictionary:anchorInputs error:&error];
          if (!maskProvider) fail(error, "Ditto temporal anchor inputs");
        }
        auto mask = [state_->motionMask predictionFromFeatures:maskProvider error:&error];
        if (!mask) fail(error, "Ditto temporal mask prediction");
        require(mask.featureNames.count == 2, "Ditto temporal mask output count");
        for (NSString* name in @[maskName, occlusionName]) {
          auto array = [mask featureValueForName:name].multiArrayValue;
          auto shape = state_->motionMask.modelDescription.outputDescriptionsByName[name].multiArrayConstraint.shape;
          require(array && array.dataType == MLMultiArrayDataTypeFloat32 &&
                      [array.shape isEqualToArray:shape], "Ditto temporal mask output shape/type");
        }
        state_->cachedMask = mask;
        state_->maskMs += milliseconds(prepared, Clock::now());
        ++state_->maskRuns;
      }
      // Retain the native tensors; every frame still samples the source feature
      // with the current driving keypoints and decodes a newly generated face.
      auto combined = [state_->inputs mutableCopy];
      combined[maskName] = [state_->cachedMask featureValueForName:maskName];
      combined[occlusionName] = [state_->cachedMask featureValueForName:occlusionName];
      provider = [[MLDictionaryFeatureProvider alloc] initWithDictionary:combined error:&error];
      if (!provider) fail(error, "Ditto temporal warp inputs");
    }
    const auto applyStarted = Clock::now();
    auto output = [state_->model predictionFromFeatures:provider error:&error];
    const auto predicted = Clock::now();
    if (!output) fail(error, "Ditto warp prediction");
    state_->lastFrame = frameIndex;
    if (state_->motionMask) state_->applyMs += milliseconds(applyStarted, predicted);
    MLMultiArray* bridge = [output featureValueForName:@"_w_Mul_output_0"].multiArrayValue;
    require(output.featureNames.count == 1 && bridge &&
                bridge.dataType == MLMultiArrayDataTypeFloat32 &&
                [bridge.shape isEqualToArray:@[@1, @256, @64, @64]], "Ditto warp bridge shape/type");
    PreparedPtr result(new Prepared());
    result->output_ = output;
    result->owner_ = state_.get();
    const auto finished = Clock::now();
    auto& timing = state_->sourceTiming;
    timing.inputMs += milliseconds(started, prepared);
    timing.predictMs += milliseconds(prepared, predicted);
    timing.readbackMs += milliseconds(predicted, finished);
    ++timing.runs;
    return result;
  }
  } catch (...) {
    // sourceProvider may have overwritten the input buffers before prediction
    // failed. Never let a retry associate an old mask with those new buffers.
    state_->cachedMask = nil;
    state_->lastFrame = -2;
    throw;
  }
}
std::vector<float> CoreMLGenerator::finish(PreparedPtr prepared) {
  require(split() && prepared && prepared->owner_ == state_.get(), "Ditto decoder prepared owner");
  @autoreleasepool {
    const auto started = Clock::now();
    NSError* error = nil;
    // Keep the native feature provider/MLMultiArray; no intermediate CPU copy.
    auto output = [state_->decoder predictionFromFeatures:prepared->output_ error:&error];
    const auto predicted = Clock::now();
    if (!output) fail(error, "Ditto decoder prediction");
    auto result = faceValues(output, state_->outputName);
    const auto finished = Clock::now();
    auto& timing = state_->decoderTiming;
    timing.predictMs += milliseconds(started, predicted);
    timing.readbackMs += milliseconds(predicted, finished);
    ++timing.runs;
    return result;
  }
}
std::vector<float> CoreMLGenerator::run(const std::vector<std::string>& names,
    const std::vector<std::vector<float>>& data, const std::vector<std::vector<int64_t>>& shapes) {
  if (split()) return finish(prepare(names, data, shapes));
  @autoreleasepool {
    const auto started = Clock::now();
    auto provider = sourceProvider(state_->model, state_->inputs, names, inputRefs(data), shapes);
    const auto prepared = Clock::now();
    NSError* error = nil;
    auto output = [state_->model predictionFromFeatures:provider error:&error];
    const auto predicted = Clock::now();
    if (!output) fail(error, "LivePortrait CoreML prediction");
    auto result = faceValues(output, state_->outputName);
    const auto finished = Clock::now();
    auto& timing = state_->sourceTiming;
    timing.inputMs += milliseconds(started, prepared);
    timing.predictMs += milliseconds(prepared, predicted);
    timing.readbackMs += milliseconds(predicted, finished);
    ++timing.runs;
    return result;
  }
}
}
