#include "TtsBackend.h"

#include "NativeTensor.h"
#include "OrtRuntime.h"
#include "VoiceTextFrontend.h"

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace videocut::ai::native::detail {
namespace {

constexpr std::int32_t kPromptSampleRateHz = 16000;
constexpr std::int32_t kOutputSampleRateHz = 24000;
constexpr std::size_t kLayerCount = 17;
constexpr std::size_t kHeadSize = 128;
constexpr std::size_t kPastTensorCount = kLayerCount * 2;
constexpr std::size_t kVocabularySize = 17667;
constexpr std::size_t kMinimumNewTokens = 10;
constexpr std::size_t kMinimumGenerationTokens = kMinimumNewTokens + 1U;
constexpr std::size_t kMaximumGenerationTokens = 2048;
constexpr std::size_t kMaximumContextTokens = 4096;
constexpr float kRepetitionPenalty = 1.1F;
constexpr float kSamplingTemperature = 0.8F;
constexpr double kSamplingTopP = 0.95;

struct GenerationResult final {
  std::vector<std::int64_t> tokens;
  bool hitEos{false};
};

class InferenceCanceled final : public std::exception {
public:
  const char *what() const noexcept final { return "Canceled."; }
};

void ThrowIfCanceled(const InferenceRequest &request) {
  if (request.IsCancellationRequested())
    throw InferenceCanceled{};
}

const model::ResolvedModelFile *
RequireModelFile(const model::ResolvedModel &model, const char *role) {
  const auto *file = model.FileForRole(role);
  if (file == nullptr)
    throw std::invalid_argument(std::string("model role is missing: ") + role);
  return file;
}

void RequireNames(const std::vector<std::string> &actual,
                  const std::vector<std::string> &expected,
                  const char *modelName) {
  if (actual != expected) {
    throw std::runtime_error(std::string(modelName) +
                             " ONNX tensor contract changed");
  }
}

std::vector<std::string> LanguageModelInputNames() {
  std::vector<std::string> names{"input_ids", "attention_mask", "position_ids"};
  for (std::size_t layer = 0; layer < kLayerCount; ++layer) {
    names.push_back("past_key_values." + std::to_string(layer) + ".key");
    names.push_back("past_key_values." + std::to_string(layer) + ".value");
  }
  return names;
}

std::vector<std::string> LanguageModelOutputNames() {
  std::vector<std::string> names{"logits"};
  for (std::size_t layer = 0; layer < kLayerCount; ++layer) {
    names.push_back("present." + std::to_string(layer) + ".key");
    names.push_back("present." + std::to_string(layer) + ".value");
  }
  return names;
}

ActualComputePath MakeComputePath(const ComputeBackend backend) {
  ActualComputePath path;
  path.selected = backend;
  if (backend == ComputeBackend::CoreMl) {
    path.deviceClass = "Apple Core ML";
  } else if (backend == ComputeBackend::Metal) {
    path.deviceClass = "Apple CPU encoder/LM plus Metal WebGPU codec decoder";
  } else {
    path.deviceClass = "Apple CPU";
  }
  path.hardwareEvidenceConfirmed = backend == ComputeBackend::Cpu;
  return path;
}

std::vector<Ort::Float16_t> ToFloat16(const std::vector<float> &values) {
  std::vector<Ort::Float16_t> output;
  output.reserve(values.size());
  for (const float value : values)
    output.emplace_back(value);
  return output;
}

std::vector<float> FromFloat16(const Ort::Value &value) {
  const std::size_t count = value.GetTensorTypeAndShapeInfo().GetElementCount();
  const Ort::Float16_t *input = value.GetTensorData<Ort::Float16_t>();
  std::vector<float> output;
  output.reserve(count);
  for (std::size_t index = 0; index < count; ++index)
    output.push_back(input[index].ToFloat());
  return output;
}

Ort::Value Int64Tensor(Ort::MemoryInfo &memory,
                       std::vector<std::int64_t> &values,
                       const std::vector<std::int64_t> &shape) {
  return Ort::Value::CreateTensor<std::int64_t>(
      memory, values.data(), values.size(), shape.data(), shape.size());
}

Ort::Value Float16Tensor(Ort::MemoryInfo &memory,
                         std::vector<Ort::Float16_t> &values,
                         const std::vector<std::int64_t> &shape) {
  return Ort::Value::CreateTensor<Ort::Float16_t>(
      memory, values.data(), values.size(), shape.data(), shape.size());
}

std::vector<std::int64_t>
EncodeReference(OrtModelSession &encoder,
                const std::vector<float> &promptAudio) {
  std::vector<Ort::Float16_t> samples = ToFloat16(promptAudio);
  Ort::MemoryInfo memory = CpuMemoryInfo();
  const std::vector<std::int64_t> shape{
      1, 1, static_cast<std::int64_t>(samples.size())};
  Ort::Value input = Float16Tensor(memory, samples, shape);
  const auto &inputNames = encoder.InputNamePointers();
  const auto &outputNames = encoder.OutputNamePointers();
  auto outputs =
      encoder.Session().Run(Ort::RunOptions{nullptr}, inputNames.data(), &input,
                            1, outputNames.data(), outputNames.size());
  if (outputs.size() != 1 ||
      outputs[0].GetTensorTypeAndShapeInfo().GetElementType() !=
          ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64) {
    throw std::runtime_error(
        "voice synthesis codec encoder returned invalid VQ codes");
  }
  const std::size_t count =
      outputs[0].GetTensorTypeAndShapeInfo().GetElementCount();
  const std::int64_t *codes = outputs[0].GetTensorData<std::int64_t>();
  return std::vector<std::int64_t>(codes, codes + count);
}

std::int64_t SelectSpeechToken(const Ort::Value &logits,
                               const std::vector<std::int64_t> &allTokens,
                               const std::int64_t eosToken,
                               const std::size_t generatedCount,
                               std::mt19937_64 &random) {
  const auto shape = TensorShape(logits);
  if (shape.size() != 3 || shape[0] != 1 ||
      shape[2] != static_cast<std::int64_t>(kVocabularySize) || shape[1] <= 0) {
    throw std::runtime_error(
        "voice synthesis language model logits shape changed");
  }
  const Ort::Float16_t *values = logits.GetTensorData<Ort::Float16_t>();
  const std::size_t lastOffset =
      (static_cast<std::size_t>(shape[1]) - 1U) * kVocabularySize;
  std::set<std::int64_t> repeated(allTokens.begin(), allTokens.end());
  float bestScore = -std::numeric_limits<float>::infinity();
  std::vector<std::pair<float, std::int64_t>> candidates;
  candidates.reserve(kVocabularySize);
  for (std::size_t token = 0; token < kVocabularySize; ++token) {
    float score = values[lastOffset + token].ToFloat();
    if (repeated.count(static_cast<std::int64_t>(token)) != 0) {
      score = score < 0.0F ? score * kRepetitionPenalty
                           : score / kRepetitionPenalty;
    }
    if (generatedCount < kMinimumNewTokens &&
        static_cast<std::int64_t>(token) == eosToken) {
      score = -std::numeric_limits<float>::infinity();
    }
    if (!std::isfinite(score))
      continue;
    score /= kSamplingTemperature;
    bestScore = std::max(bestScore, score);
    candidates.emplace_back(score, static_cast<std::int64_t>(token));
  }
  if (!std::isfinite(bestScore))
    throw std::runtime_error(
        "voice synthesis language model produced no finite token");
  // The shipped Hojo model uses temperature/top-p sampling. Greedy decoding
  // can become trapped in repeated silence codes and never produce EOS.
  std::sort(candidates.begin(), candidates.end(),
            [](const auto &left, const auto &right) {
              return left.first != right.first ? left.first > right.first
                                               : left.second < right.second;
            });
  std::vector<double> weights;
  weights.reserve(candidates.size());
  double total = 0.0;
  for (const auto &candidate : candidates) {
    const double weight = std::exp(static_cast<double>(candidate.first - bestScore));
    weights.push_back(weight);
    total += weight;
  }
  double retained = 0.0;
  std::size_t count = 0;
  do {
    retained += weights[count++];
  } while (count < weights.size() && retained < total * kSamplingTopP);
  // Use explicit conversion rather than a library distribution so replay does
  // not depend on the standard library's distribution implementation.
  const double draw = static_cast<double>(random() >> 11U) * 0x1.0p-53 * retained;
  double cumulative = 0.0;
  for (std::size_t index = 0; index < count; ++index) {
    cumulative += weights[index];
    if (draw < cumulative)
      return candidates[index].second;
  }
  return candidates[count - 1U].second;
}

GenerationResult Generate(OrtModelSession &languageModel,
                          const std::vector<std::int64_t> &promptIds,
                          const std::int64_t eosToken,
                          const std::size_t maximumNewTokens,
                          const InferenceRequest &request) {
  if (promptIds.empty())
    throw std::invalid_argument(
        "voice synthesis prompt token sequence is empty");
  if (promptIds.size() > kMaximumContextTokens ||
      maximumNewTokens > kMaximumContextTokens - promptIds.size()) {
    throw std::logic_error(
        "voice synthesis internal generation budget exceeds its context");
  }

  Ort::MemoryInfo memory = CpuMemoryInfo();
  std::vector<std::int64_t> allTokens = promptIds;
  GenerationResult result;
  result.tokens.reserve(maximumNewTokens);
  std::vector<std::int64_t> currentIds = promptIds;
  std::vector<std::int64_t> attention(promptIds.size(), 1);
  std::vector<std::int64_t> positions(promptIds.size());
  for (std::size_t index = 0; index < positions.size(); ++index)
    positions[index] = static_cast<std::int64_t>(index);

  std::vector<Ort::Value> past;
  past.reserve(kPastTensorCount);
  const std::vector<std::int64_t> emptyPastShape{1, 1, 0, kHeadSize};
  for (std::size_t index = 0; index < kPastTensorCount; ++index) {
    past.push_back(Ort::Value::CreateTensor<Ort::Float16_t>(
        memory, nullptr, 0, emptyPastShape.data(), emptyPastShape.size()));
  }

  const auto &inputNames = languageModel.InputNamePointers();
  const auto &outputNames = languageModel.OutputNamePointers();
  // Reset per generation, never share sampling state across jobs or chunks.
  std::mt19937_64 random{42};
  for (std::size_t step = 0; step < maximumNewTokens; ++step) {
    ThrowIfCanceled(request);
    const std::vector<std::int64_t> idShape{
        1, static_cast<std::int64_t>(currentIds.size())};
    const std::vector<std::int64_t> attentionShape{
        1, static_cast<std::int64_t>(attention.size())};
    const std::vector<std::int64_t> positionShape{
        1, static_cast<std::int64_t>(positions.size())};
    std::vector<Ort::Value> inputs;
    inputs.reserve(3 + kPastTensorCount);
    inputs.push_back(Int64Tensor(memory, currentIds, idShape));
    inputs.push_back(Int64Tensor(memory, attention, attentionShape));
    inputs.push_back(Int64Tensor(memory, positions, positionShape));
    for (Ort::Value &cache : past)
      inputs.push_back(std::move(cache));

    std::vector<Ort::Value> outputs = languageModel.Session().Run(
        Ort::RunOptions{nullptr}, inputNames.data(), inputs.data(),
        inputs.size(), outputNames.data(), outputNames.size());
    if (outputs.size() != 1 + kPastTensorCount)
      throw std::runtime_error(
          "voice synthesis language model output count changed");

    const std::int64_t token =
        SelectSpeechToken(outputs[0], allTokens, eosToken, result.tokens.size(),
                          random);
    result.tokens.push_back(token);
    allTokens.push_back(token);
    if (token == eosToken && result.tokens.size() >= kMinimumNewTokens) {
      result.hitEos = true;
      break;
    }

    past.clear();
    past.reserve(kPastTensorCount);
    for (std::size_t index = 1; index < outputs.size(); ++index)
      past.push_back(std::move(outputs[index]));
    currentIds.assign(1, token);
    attention.assign(allTokens.size(), 1);
    positions.assign(1, static_cast<std::int64_t>(allTokens.size() - 1U));
  }
  return result;
}

std::vector<float> DecodeCodes(OrtModelSession &decoder,
                               const std::vector<std::int64_t> &codes) {
  if (codes.empty())
    throw std::runtime_error("voice synthesis generated no audio VQ codes");
  std::vector<std::int64_t> mutableCodes = codes;
  const std::vector<std::int64_t> shape{
      1, 1, static_cast<std::int64_t>(mutableCodes.size())};
  Ort::MemoryInfo memory = CpuMemoryInfo();
  Ort::Value input = Int64Tensor(memory, mutableCodes, shape);
  const auto &inputNames = decoder.InputNamePointers();
  const auto &outputNames = decoder.OutputNamePointers();
  auto outputs =
      decoder.Session().Run(Ort::RunOptions{nullptr}, inputNames.data(), &input,
                            1, outputNames.data(), outputNames.size());
  if (outputs.size() != 1 ||
      outputs[0].GetTensorTypeAndShapeInfo().GetElementType() !=
          ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16) {
    throw std::runtime_error(
        "voice synthesis codec decoder returned invalid waveform");
  }
  return FromFloat16(outputs[0]);
}

class TtsSession final : public IInferenceSession {
public:
  TtsSession(model::ResolvedModel model, const ComputeBackend backend,
             NativeSdkOptions options)
      : m_model(std::move(model)), m_options(options),
        m_tokenizer(RequireModelFile(m_model, "tts.tokenizer")->path),
        m_encoder(RequireModelFile(m_model, "tts.encoder")->path,
                  backend == ComputeBackend::Metal ? ComputeBackend::Cpu
                                                   : backend,
                  options.inferenceThreads),
        m_languageModel(RequireModelFile(m_model, "tts.language_model")->path,
                        backend == ComputeBackend::Metal ? ComputeBackend::Cpu
                                                         : backend,
                        options.inferenceThreads),
        m_decoder(RequireModelFile(m_model, "tts.decoder")->path, backend,
                  options.inferenceThreads),
        m_computePath(MakeComputePath(backend)) {
    RequireNames(m_encoder.InputNames(), {"wav"},
                 "voice synthesis encoder input");
    RequireNames(m_encoder.OutputNames(), {"vq_codes"},
                 "voice synthesis encoder output");
    RequireNames(m_languageModel.InputNames(), LanguageModelInputNames(),
                 "voice synthesis language model input");
    RequireNames(m_languageModel.OutputNames(), LanguageModelOutputNames(),
                 "voice synthesis language model output");
    RequireNames(m_decoder.InputNames(), {"vq_codes"},
                 "voice synthesis decoder input");
    RequireNames(m_decoder.OutputNames(), {"wav_24k"},
                 "voice synthesis decoder output");
    if (m_tokenizer.VocabularySize() != kVocabularySize)
      throw std::runtime_error(
          "voice synthesis tokenizer/model vocabulary sizes do not match");
    const auto *tnTagger = m_model.FileForRole("text.tn_tagger_fst");
    const auto *tnVerbalizer =
        m_model.FileForRole("text.tn_verbalizer_fst");
    if ((tnTagger == nullptr) != (tnVerbalizer == nullptr))
      throw std::runtime_error(
          "TTS structured TN tagger/verbalizer roles are incomplete");
    if (tnTagger && tnVerbalizer) {
      auto normalizer = FstTextNormalizer::LoadStructured(
          tnTagger->path, tnVerbalizer->path,
          TextNormalizationDirection::WrittenToSpoken,
          m_options.ttsTextNormalizationFsts);
      if (!normalizer.ok())
        throw std::runtime_error(normalizer.error().message);
      m_textNormalizer = normalizer.takeValue();
    } else if (!m_options.ttsTextNormalizationFsts.empty()) {
      auto normalizer =
          FstTextNormalizer::Load(m_options.ttsTextNormalizationFsts);
      if (!normalizer.ok())
        throw std::runtime_error(normalizer.error().message);
      m_textNormalizer = normalizer.takeValue();
    }
  }

  std::size_t MaximumBatchSize() const noexcept final {
    return m_model.spec.maximumBatchSize;
  }
  std::uint64_t ResidentBytes() const noexcept final {
    return m_model.spec.estimatedResidentBytes;
  }
  ActualComputePath ComputePath() const final { return m_computePath; }

  Result<std::vector<InferenceResponse>>
  RunBatch(const std::vector<InferenceRequest> &requests) final {
    try {
      std::vector<InferenceResponse> responses;
      responses.reserve(requests.size());
      for (const InferenceRequest &request : requests) {
        ThrowIfCanceled(request);
        auto text = ReadRequiredUtf8(request, kText);
        if (!text.ok())
          return Result<std::vector<InferenceResponse>>::Failure(text.error());
        auto promptText = ReadRequiredUtf8(request, kPromptText);
        if (!promptText.ok()) {
          return Result<std::vector<InferenceResponse>>::Failure(
              promptText.error());
        }
        auto promptAudio = ReadRequiredFloat(request, kPromptAudio);
        if (!promptAudio.ok()) {
          return Result<std::vector<InferenceResponse>>::Failure(
              promptAudio.error());
        }
        auto promptRate = ReadScalarInt32(request, kPromptSampleRate);
        if (!promptRate.ok()) {
          return Result<std::vector<InferenceResponse>>::Failure(
              promptRate.error());
        }
        auto requestedMaximum = ReadOptionalScalarInt64(
            request, kMaximumTokens,
            static_cast<std::int64_t>(m_options.ttsMaximumTokens));
        if (!requestedMaximum.ok()) {
          return Result<std::vector<InferenceResponse>>::Failure(
              requestedMaximum.error());
        }
        if (text.value().empty() || promptText.value().empty() ||
            promptAudio.value().empty() ||
            promptRate.value() != kPromptSampleRateHz ||
            requestedMaximum.value() <
                static_cast<std::int64_t>(kMinimumGenerationTokens) ||
            requestedMaximum.value() >
                static_cast<std::int64_t>(kMaximumGenerationTokens)) {
          return Result<std::vector<InferenceResponse>>::Failure(
              AiErrorCode::InvalidArgument,
              "voice synthesis requires non-empty text/reference, mono 16 kHz "
              "prompt "
              "audio, and 11..2048 generation tokens");
        }

        ThrowIfCanceled(request);
        const std::vector<std::int64_t> &referenceCodes =
            ReferenceCodes(promptAudio.value());
        ThrowIfCanceled(request);
        std::string normalizedText = text.value();
        std::string normalizedPromptText = promptText.value();
        if (m_textNormalizer) {
          auto target = m_textNormalizer->Normalize(normalizedText);
          auto prompt = m_textNormalizer->Normalize(normalizedPromptText);
          if (!target.ok() || !prompt.ok()) {
            return Result<std::vector<InferenceResponse>>::Failure(
                target.ok() ? prompt.error() : target.error());
          }
          normalizedText = std::move(target).takeValue().text;
          normalizedPromptText = std::move(prompt).takeValue().text;
        }
        const std::vector<std::int64_t> promptIds = m_tokenizer.EncodePrompt(
            normalizedPromptText, normalizedText, referenceCodes);
        if (promptIds.size() >
            kMaximumContextTokens - kMinimumGenerationTokens) {
          return Result<std::vector<InferenceResponse>>::Failure(
              AiErrorCode::InvalidArgument,
              std::string(kTtsContextLimitErrorPrefix) +
                  " voice synthesis prompt leaves fewer than 11 generation "
                  "context tokens (prompt_tokens=" +
                  std::to_string(promptIds.size()) +
                  ", context_tokens=" +
                  std::to_string(kMaximumContextTokens) + ")");
        }
        const std::size_t remainingContext =
            kMaximumContextTokens - promptIds.size();
        const std::size_t effectiveMaximum =
            std::min<std::size_t>(
                static_cast<std::size_t>(requestedMaximum.value()),
                remainingContext);
        GenerationResult generation =
            Generate(m_languageModel, promptIds, m_tokenizer.SpeechEndId(),
                     effectiveMaximum, request);
        if (!generation.hitEos) {
          throw std::runtime_error(
              std::string(kTtsEosLimitErrorPrefix) +
              " voice synthesis exhausted its effective generation budget "
              "without reaching EOS; requested_maximum_tokens=" +
              std::to_string(requestedMaximum.value()) +
              ", effective_maximum_tokens=" +
              std::to_string(effectiveMaximum) +
              ", prompt_tokens=" + std::to_string(promptIds.size()) +
              ", context_tokens=" + std::to_string(kMaximumContextTokens) +
              "; the text chunk was not decoded");
        }
        std::vector<std::int64_t> generated =
            std::move(generation.tokens);
        std::vector<std::int64_t> audioCodes;
        for (const std::int64_t id : generated) {
          if (id == m_tokenizer.SpeechEndId())
            break;
          const auto code = m_tokenizer.AudioCodeForId(id);
          if (code)
            audioCodes.push_back(*code);
        }
        ThrowIfCanceled(request);
        std::vector<float> waveform = DecodeCodes(m_decoder, audioCodes);
        ThrowIfCanceled(request);

        InferenceResponse response;
        response.requestId = request.requestId;
        response.outputs.push_back(MakeFloatOutput(
            kTtsWaveform, {static_cast<std::int64_t>(waveform.size())},
            std::move(waveform)));
        response.outputs.push_back(
            MakeInt32Output(kTtsSampleRate, {1}, {kOutputSampleRateHz}));
        response.outputs.push_back(
            MakeInt32Output(kTtsHitEos, {1}, {1}));
        response.outputs.push_back(MakeInt32Output(
            kTtsEffectiveMaximumTokens, {1},
            {static_cast<std::int32_t>(effectiveMaximum)}));
        response.outputs.push_back(
            MakeInt64Output(kTtsReferenceAudioCodes,
                            {static_cast<std::int64_t>(referenceCodes.size())},
                            std::vector<std::int64_t>(referenceCodes)));
        response.outputs.push_back(MakeInt64Output(
            kTtsPromptTokenIds, {static_cast<std::int64_t>(promptIds.size())},
            std::vector<std::int64_t>(promptIds)));
        response.outputs.push_back(
            MakeInt64Output(kTtsGeneratedTokenIds,
                            {static_cast<std::int64_t>(generated.size())},
                            std::vector<std::int64_t>(generated)));
        response.outputs.push_back(MakeInt64Output(
            kTtsAudioCodes, {static_cast<std::int64_t>(audioCodes.size())},
            std::move(audioCodes)));
        responses.push_back(std::move(response));
      }
      return Result<std::vector<InferenceResponse>>::Success(
          std::move(responses));
    } catch (const InferenceCanceled &) {
      return Result<std::vector<InferenceResponse>>::Failure(
          AiErrorCode::Canceled, "Canceled.");
    } catch (const Ort::Exception &exception) {
      return Result<std::vector<InferenceResponse>>::Failure(
          AiErrorCode::InferenceFailed,
          std::string("voice synthesis ONNX inference failed: ") +
              exception.what());
    } catch (const std::exception &exception) {
      return Result<std::vector<InferenceResponse>>::Failure(
          AiErrorCode::InferenceFailed, exception.what());
    }
  }

private:
  const std::vector<std::int64_t> &
  ReferenceCodes(const std::vector<float> &promptAudio) {
    if (!m_hasCachedReference || m_cachedPromptAudio != promptAudio) {
      std::vector<std::int64_t> encoded =
          EncodeReference(m_encoder, promptAudio);
      m_cachedPromptAudio = promptAudio;
      m_cachedReferenceCodes = std::move(encoded);
      m_hasCachedReference = true;
    }
    return m_cachedReferenceCodes;
  }

  model::ResolvedModel m_model;
  NativeSdkOptions m_options;
  VoiceTextFrontend m_tokenizer;
  std::shared_ptr<FstTextNormalizer> m_textNormalizer;
  OrtModelSession m_encoder;
  OrtModelSession m_languageModel;
  OrtModelSession m_decoder;
  ActualComputePath m_computePath;
  std::vector<float> m_cachedPromptAudio;
  std::vector<std::int64_t> m_cachedReferenceCodes;
  bool m_hasCachedReference{false};
};

class TtsBackendFactory final : public model::IInferenceBackendFactory {
public:
  TtsBackendFactory(const ComputeBackend backend, NativeSdkOptions options)
      : m_backend(backend), m_options(options) {}

  std::string RuntimeId() const final { return "onnxruntime-optimum"; }
  std::string RuntimeVersion() const final {
    return VIDEOCUT_AI_NATIVE_ORT_VERSION;
  }
  ComputeBackend Backend() const noexcept final { return m_backend; }
  bool IsAvailable() const noexcept final {
    return m_backend == ComputeBackend::Cpu ||
           m_backend == ComputeBackend::CoreMl ||
           m_backend == ComputeBackend::Metal;
  }
  Result<std::shared_ptr<IInferenceSession>>
  CreateSession(const model::ResolvedModel &model) final {
    if (model.spec.task != AiTask::Tts) {
      return Result<std::shared_ptr<IInferenceSession>>::Failure(
          AiErrorCode::BackendUnavailable,
          "voice synthesis factory received a non-TTS model");
    }
    try {
      return Result<std::shared_ptr<IInferenceSession>>::Success(
          std::make_shared<TtsSession>(model, m_backend, m_options));
    } catch (const std::exception &exception) {
      return Result<std::shared_ptr<IInferenceSession>>::Failure(
          AiErrorCode::SessionCreationFailed, exception.what());
    }
  }

private:
  ComputeBackend m_backend;
  NativeSdkOptions m_options;
};

} // namespace

std::shared_ptr<model::IInferenceBackendFactory>
CreateTtsBackendFactory(const ComputeBackend backend,
                        const NativeSdkOptions &options) {
  return std::make_shared<TtsBackendFactory>(backend, options);
}

} // namespace videocut::ai::native::detail
