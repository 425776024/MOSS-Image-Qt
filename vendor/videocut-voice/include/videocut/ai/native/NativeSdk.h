#pragma once

#include "videocut/ai/Inference.h"
#include "videocut/ai/model/AiRuntime.h"
#include "videocut/ai/native/TextNormalization.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace videocut::ai::native {

inline constexpr const char *kVadModelId =
    "com.videocut.speech.vad.v1";
inline constexpr const char *kAsrModelId =
    "com.videocut.speech.asr.zh-en.v1";
inline constexpr const char *kTtsModelId =
    "com.videocut.speech.tts.v1";
inline constexpr const char *kEnhancementModelId =
    "com.videocut.audio.enhancement.v1";
inline constexpr const char *kSeparationModelId =
    "com.videocut.audio.separation.4stem.v1";
inline constexpr const char *kFaceDetectorModelId =
    "com.videocut.vision.face-detector.v1";
inline constexpr const char *kFaceEmbedderModelId =
    "com.videocut.vision.face-embedder.v1";
inline constexpr const char *kFaceLandmarksModelId =
    "com.videocut.vision.face-landmarks.v1";
inline constexpr const char *kVlmModelId =
    "com.videocut.vision.understanding.v1";
inline constexpr const char *kPersonSegmentationModelId =
    "com.videocut.vision.person-segmentation.v1";
inline constexpr const char *kSemanticSegmentationModelId =
    "com.videocut.vision.selfie-multiclass.v1";
inline constexpr const char *kPersonDetectorModelId =
    "com.videocut.vision.person-detector.v1";
inline constexpr const char *kBodyPoseModelId =
    "com.videocut.vision.body-pose.v1";
inline constexpr const char *kPalmDetectorModelId =
    "com.videocut.vision.palm-detector.v1";
inline constexpr const char *kHandPoseModelId =
    "com.videocut.vision.hand-pose.v1";
inline constexpr const char *kDepthEstimationModelId =
    "com.videocut.vision.depth-anything-v2-small.v1";

// Stable tensor names at the native SDK boundary. Text is UTF-8 stored in a
// UInt8 tensor so the generic value contract remains ABI-neutral.
inline constexpr const char *kAudioSamples = "audio.samples";
inline constexpr const char *kAudioSampleRate = "audio.sample_rate";
inline constexpr const char *kText = "text.utf8";
inline constexpr const char *kPromptText = "prompt.text.utf8";
inline constexpr const char *kPromptAudio = "prompt.audio.samples";
inline constexpr const char *kPromptSampleRate = "prompt.audio.sample_rate";
inline constexpr const char *kImageBgr = "image.bgr";
inline constexpr const char *kImageRgb = "image.rgb";
inline constexpr const char *kFaceDetection = "face.detection";
inline constexpr const char *kMaximumTokens = "generation.maximum_tokens";
/// NCHW [1, 3, 192, 192] normalized RGB input for the locked PPHumanSeg
/// contract. Keeping it distinct from generic image tensors prevents callers
/// from accidentally skipping the documented resize and normalization step.
inline constexpr const char *kPersonSegmentationImage =
    "person_segmentation.image";
/// NHWC [1, 256, 256, 3] RGB input normalized with the public MediaPipe
/// model's attached (mean=127.5, std=127.5) metadata.
inline constexpr const char *kSemanticSegmentationImage =
    "semantic_segmentation.image";
/// NHWC [1, 256, 256, 3] RGB face ROI in the Face Landmarker model's public
/// [0, 1] input range. The calling bridge owns detection and ROI alignment.
inline constexpr const char *kFaceLandmarksImage = "face_landmarks.image";
/// NCHW [1, 3, 224, 224] RGB input letterboxed to a square and normalized to
/// [-1, 1] for the locked MediaPipe person detector.
inline constexpr const char *kPersonDetectionImage = "person_detection.image";
/// NHWC [1, 256, 256, 3] RGB body ROI. Pixels outside the oriented ROI are
/// black; valid pixels retain their [0, 1] source range.
inline constexpr const char *kBodyPoseImage = "body_pose.image";
/// NHWC [1, 192, 192, 3] RGB letterboxed to the public MediaPipe palm
/// detector's [0, 1] input domain.
inline constexpr const char *kPalmDetectionImage = "palm_detection.image";
/// NHWC [1, 224, 224, 3] RGB from one oriented palm ROI in the public
/// MediaPipe hand-landmark model's [0, 1] input domain.
inline constexpr const char *kHandPoseImage = "hand_pose.image";
/// NCHW [1, 3, 518, 518] RGB input for the locked Depth Anything V2 Small
/// ONNX graph. Values are normalized with RGB mean [123.675, 116.28, 103.53]
/// and standard deviation [58.395, 57.12, 57.375].
inline constexpr const char *kDepthEstimationImage = "depth_estimation.image";

inline constexpr const char *kVadSegments = "vad.segments";
inline constexpr const char *kAsrText = "asr.text.utf8";
inline constexpr const char *kAsrPunctuatedText =
    "asr.punctuated_text.utf8";
inline constexpr const char *kAsrTimestamps = "asr.timestamps";
inline constexpr const char *kAsrTokensJson = "asr.tokens.json.utf8";
inline constexpr const char *kTtsWaveform = "tts.waveform";
inline constexpr const char *kTtsSampleRate = "tts.sample_rate";
inline constexpr const char *kTtsReferenceAudioCodes =
    "tts.reference_audio_codes";
inline constexpr const char *kTtsPromptTokenIds = "tts.prompt_token_ids";
inline constexpr const char *kTtsGeneratedTokenIds =
    "tts.generated_token_ids";
inline constexpr const char *kTtsAudioCodes = "tts.audio_codes";
inline constexpr const char *kTtsHitEos = "tts.hit_eos";
inline constexpr const char *kTtsEffectiveMaximumTokens =
    "tts.effective_maximum_tokens";
inline constexpr const char *kTtsEosLimitErrorPrefix = "tts_eos_limit:";
inline constexpr const char *kTtsContextLimitErrorPrefix =
    "tts_context_limit:";
inline constexpr const char *kSeparatedStems = "separation.stems";
inline constexpr const char *kFaceDetections = "face.detections";
inline constexpr const char *kFaceEmbedding = "face.embedding";
inline constexpr const char *kVlmJson = "vlm.json.utf8";
/// NCHW [1, 2, 192, 192] foreground/background logits emitted by the locked
/// person-segmentation model. Consumers convert it to a soft foreground mask
/// at the effect boundary; the model backend never invents semantics.
inline constexpr const char *kPersonSegmentationLogits =
    "person_segmentation.logits";
/// NHWC [1, 256, 256, 6] logits in this stable channel order: background,
/// hair, body skin, face skin, clothes, accessories.
inline constexpr const char *kSemanticSegmentationLogits =
    "semantic_segmentation.logits";
/// [1, 1, 1, 1434] raw 478 x 3 Face Landmarker coordinates in its input ROI.
inline constexpr const char *kFaceLandmarks = "face_landmarks.points";
/// [1, 1, 1, 1] raw local face-presence logit for the Face Landmarker ROI.
inline constexpr const char *kFaceLandmarksPresence =
    "face_landmarks.presence";
/// [1, 2254, 1] person-detector confidence logits.
inline constexpr const char *kPersonDetectionScores =
    "person_detection.scores";
/// [1, 2254, 12] detector box and keypoint regressions.
inline constexpr const char *kPersonDetectionRegressions =
    "person_detection.regressions";
/// [1, 195] raw 39 x 5 body-pose landmarks in the oriented model ROI.
inline constexpr const char *kBodyPoseLandmarks = "body_pose.landmarks";
/// [1, 1] body-pose presence confidence.
inline constexpr const char *kBodyPoseConfidence = "body_pose.confidence";
/// [1, 2016, 1] raw palm-detector confidence logits.
inline constexpr const char *kPalmDetectionScores =
    "palm_detection.scores";
/// [1, 2016, 18] raw palm box and seven-keypoint regressions.
inline constexpr const char *kPalmDetectionRegressions =
    "palm_detection.regressions";
/// [1, 63] raw 21 x 3 hand landmarks in the oriented model ROI.
inline constexpr const char *kHandPoseLandmarks = "hand_pose.landmarks";
/// [1, 1] local hand-presence confidence.
inline constexpr const char *kHandPoseConfidence = "hand_pose.confidence";
/// [1, 1] handedness score emitted by the public model.
inline constexpr const char *kHandPoseHandedness = "hand_pose.handedness";
/// NCHW [1, 1, 518, 518] floating-point relative depth field. The effect
/// bridge normalizes this source-local field into the public depth2d ABI.
inline constexpr const char *kDepthEstimationMap = "depth_estimation.map";

struct NativeSdkOptions final {
  std::uint32_t inferenceThreads{4};
  std::uint32_t punctuationThreads{2};
  std::uint32_t ttsMaximumTokens{512};
  std::uint32_t vlmContextTokens{4096};
  std::uint32_t vlmBatchTokens{2048};
  std::uint32_t vlmMaximumTokens{256};
  /// Extra ASR inverse-normalization rules, applied after the model-pack rule.
  std::vector<std::filesystem::path> asrInverseTextNormalizationFsts;
  /// Optional written-to-spoken TN rules for the synthesis frontend. Do not
  /// pass an ASR inverse-normalization rule here.
  std::vector<std::filesystem::path> ttsTextNormalizationFsts;
  /// Session construction only. Product inference must not run a dummy model
  /// pass merely to populate caches; measured test hosts can opt in explicitly.
  bool vlmWarmup{false};
  /// Registers the previously rejected BS-RoFormer Core ML and Metal
  /// qualification paths. This must remain false in product hosts: Core ML
  /// exceeded 139 GB RSS, and Metal failed output-parity acceptance. Only a
  /// developer acceptance/model-lab process may explicitly opt in.
  bool enableExperimentalSeparationAccelerators{false};
  /// Registers Core ML for vision; only models whose catalog policy prefers
  /// it use acceleration. CPU-only policy and explicit CPU requests remain
  /// supported. This does not claim a particular Core ML hardware device.
  bool enableVisionAccelerators{true};
  /// Explicitly selected private compiled Apple Core ML Depth Anything P6
  /// package. Empty selects catalogued ONNX; a configured invalid package fails.
  /// This is an internal runtime option; the public Depth2d contract is fixed.
  std::filesystem::path depthAppleCoreMlModelPath;
};

struct TtsTextChunk final {
  std::string text;
  std::uint32_t tokenCount{0};
  std::uint32_t codePointCount{0};
};

struct TtsTextPlan final {
  std::uint32_t targetTokenBudget{0};
  std::vector<TtsTextChunk> chunks;
};

struct TtsTextBisect final {
  TtsTextChunk left;
  TtsTextChunk right;
};

struct TtsWaveformSeam final {
  std::uint64_t outputSampleOffset{0};
  std::uint32_t crossfadeSamples{0};
};

/// Plans UTF-8-safe sentence-preferred target chunks with the exact locked
/// tokenizer. The generation budget is converted to a conservative target-text
/// token budget for the released 50 Hz Hojo model. A one-chunk plan preserves
/// the official short-input prompt byte-for-byte.
[[nodiscard]] Result<TtsTextPlan>
PlanTtsTextChunks(const std::filesystem::path &tokenizerJson,
                  const std::string &text,
                  std::uint32_t maximumGenerationTokens);

/// Deterministically bisects a target at a UTF-8 code-point boundary, preferring
/// a balanced sentence boundary. Both returned chunks carry exact locked-
/// tokenizer counts and reconstruct [text] byte-for-byte.
[[nodiscard]] Result<TtsTextBisect>
BisectTtsTextChunk(const std::filesystem::path &tokenizerJson,
                   const std::string &text);

/// Appends one 24 kHz mono synthesis chunk using a bounded linear crossfade.
/// The input is validated before [assembled] is changed. The first chunk has
/// no seam; subsequent chunks return the exact output seam location.
[[nodiscard]] Result<std::optional<TtsWaveformSeam>>
AppendTtsWaveformChunk(std::vector<float> &assembled,
                       const std::vector<float> &chunk,
                       std::uint32_t maximumCrossfadeSamples);

/// Registers every in-process backend needed by the current model catalog.
/// This function never imports Python and never launches a command, helper,
/// server, or child process. Audio separation registers only its CPU backend
/// unless the acceptance-only experimental option is explicitly enabled.
[[nodiscard]] AiError
RegisterNativeBackends(model::AiRuntime &runtime,
                       NativeSdkOptions options = {});

[[nodiscard]] Tensor
MakeFloatTensor(std::string name, std::vector<std::int64_t> shape,
                const float *values, std::size_t count);
[[nodiscard]] Tensor
MakeInt32Tensor(std::string name, std::vector<std::int64_t> shape,
                const std::int32_t *values, std::size_t count);
[[nodiscard]] Tensor
MakeInt64Tensor(std::string name, std::vector<std::int64_t> shape,
                const std::int64_t *values, std::size_t count);
[[nodiscard]] Tensor
MakeUInt8Tensor(std::string name, std::vector<std::int64_t> shape,
                const std::uint8_t *values, std::size_t count);
[[nodiscard]] Tensor MakeUtf8Tensor(std::string name,
                                    const std::string &value);

[[nodiscard]] Result<std::string> ReadUtf8Tensor(const Tensor &tensor);
[[nodiscard]] Result<std::vector<float>>
ReadFloatTensor(const Tensor &tensor);
[[nodiscard]] Result<std::vector<std::int32_t>>
ReadInt32Tensor(const Tensor &tensor);
[[nodiscard]] Result<std::vector<std::int64_t>>
ReadInt64Tensor(const Tensor &tensor);
[[nodiscard]] Result<std::vector<std::uint8_t>>
ReadUInt8Tensor(const Tensor &tensor);

} // namespace videocut::ai::native
