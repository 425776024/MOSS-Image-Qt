#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace videocut::ai {

enum class AiTask : std::uint8_t {
  Vad = 0,
  Asr,
  Tts,
  AudioEnhancement,
  AudioSeparation,
  FaceDetection,
  FaceEmbedding,
  /// Dense local face geometry for effect anchoring. The result is transient
  /// and contains no identity embedding or persistent biometric record.
  FaceLandmarks,
  VisualUnderstanding,
  /// Per-pixel foreground probability used by local visual effects. The
  /// model contract is intentionally separate from face analysis so package
  /// effects can share one full-frame person mask without inferring identity.
  PersonSegmentation,
  /// Per-pixel semantic probabilities for the public six-class selfie model.
  /// One shared inference supplies hair, body skin, face skin, clothes,
  /// accessories, and background masks to independent effect packages.
  SemanticSegmentation,
  /// Local person detector used only to choose bounded body-pose regions.
  /// It emits transient geometry, never identity or analytics records.
  PersonDetection,
  /// Local MediaPipe-compatible body pose. The effect bridge exports only the
  /// documented 33-landmark Host-artifact ABI and derived limb regions.
  BodyPose,
  /// Local palm detector used solely to seed hand-landmark ROIs for visual
  /// effects. Its boxes remain transient and are never used as identity data.
  PalmDetection,
  /// Local 21-point hand geometry. The effect bridge exposes only transient
  /// normalized landmarks and derived gestures to effect packages.
  HandPose,
  /// Monocular relative-depth estimation for spatial effects. The model emits
  /// only a transient per-pixel field; it never identifies or persists people
  /// or scenes.
  DepthEstimation,
};

/// ModelPreferred follows the catalog order established by real-device
/// acceptance measurements. The explicit overrides remain available for
/// diagnostics and future hardware-specific retesting. VAD is always CPU-only.
enum class ComputePreference : std::uint8_t {
  AcceleratedPreferred = 0,
  CpuOnly,
  AcceleratedOnly,
  ModelPreferred,
};

enum class ComputeBackend : std::uint8_t {
  Cpu = 0,
  CoreMl,
  Metal,
  Cuda,
  Vulkan,
};

enum class SessionResidency : std::uint8_t {
  OnDemand = 0,
  KeepWarm,
  ProcessResident,
};

enum class AiErrorCode : std::uint8_t {
  None = 0,
  InvalidArgument,
  CatalogInvalid,
  ModelNotFound,
  ModelFileMissing,
  ModelFileChanged,
  ModelFileCorrupt,
  ModelPathEscapesRoot,
  BackendUnavailable,
  SessionCreationFailed,
  BatchTooLarge,
  TensorInvalid,
  InferenceFailed,
  RuntimeShuttingDown,
  AlreadyInstalled,
  Canceled,
};

struct AiError final {
  AiErrorCode code{AiErrorCode::None};
  std::string message;

  [[nodiscard]] bool ok() const noexcept { return code == AiErrorCode::None; }

  [[nodiscard]] static AiError None() { return {}; }

  [[nodiscard]] static AiError Failure(AiErrorCode code, std::string message) {
    return AiError{code, std::move(message)};
  }
};

template <typename T> class Result final {
public:
  [[nodiscard]] static Result Success(T value) {
    return Result(std::move(value));
  }

  [[nodiscard]] static Result Failure(AiErrorCode code, std::string message) {
    return Result(AiError::Failure(code, std::move(message)));
  }

  [[nodiscard]] static Result Failure(AiError error) {
    return Result(std::move(error));
  }

  [[nodiscard]] bool ok() const noexcept { return m_value.has_value(); }

  [[nodiscard]] const T &value() const {
    if (!m_value)
      throw std::logic_error("attempted to read a failed AI Result");
    return *m_value;
  }

  [[nodiscard]] T &value() {
    if (!m_value)
      throw std::logic_error("attempted to read a failed AI Result");
    return *m_value;
  }

  [[nodiscard]] T takeValue() {
    if (!m_value)
      throw std::logic_error("attempted to move a failed AI Result");
    return std::move(*m_value);
  }

  [[nodiscard]] const AiError &error() const noexcept { return m_error; }

private:
  explicit Result(T value) : m_value(std::move(value)) {}
  explicit Result(AiError error) : m_error(std::move(error)) {}

  std::optional<T> m_value;
  AiError m_error;
};

struct ActualComputePath final {
  ComputePreference requested{ComputePreference::ModelPreferred};
  ComputeBackend selected{ComputeBackend::Cpu};
  std::string modelId;
  std::string modelVersion;
  std::string modelDigest;
  std::string runtimeId;
  std::string runtimeVersion;
  std::string deviceClass;
  std::uint32_t acceleratedSubgraphCount{0};
  std::uint32_t cpuFallbackNodeCount{0};
  bool hardwareEvidenceConfirmed{false};
  // Immutable identity of the numerical implementation, effective options,
  // and any selected representation outside ModelSpec. Empty means this
  // session does not expose a persistent-result capability.
  std::string executionRecipeDigest{};
};

[[nodiscard]] const char *AiTaskName(AiTask value) noexcept;
[[nodiscard]] const char *
ComputePreferenceName(ComputePreference value) noexcept;
[[nodiscard]] const char *ComputeBackendName(ComputeBackend value) noexcept;
[[nodiscard]] const char *SessionResidencyName(SessionResidency value) noexcept;
[[nodiscard]] const char *AiErrorCodeName(AiErrorCode value) noexcept;
[[nodiscard]] bool IsAcceleratedBackend(ComputeBackend value) noexcept;

} // namespace videocut::ai
