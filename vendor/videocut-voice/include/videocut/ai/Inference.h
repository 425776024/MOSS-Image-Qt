#pragma once

#include "videocut/ai/AiTypes.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace videocut::ai {

enum class TensorElementType : std::uint8_t {
  Float32 = 0,
  Float16,
  Int64,
  Int32,
  UInt8,
  Bool,
};

struct Tensor final {
  std::string name;
  TensorElementType elementType{TensorElementType::Float32};
  std::vector<std::int64_t> shape;
  std::shared_ptr<const std::vector<std::uint8_t>> bytes;

  [[nodiscard]] AiError Validate() const;
};

struct InferenceRequest final {
  std::string requestId;
  std::vector<Tensor> inputs;

  /// Optional cooperative-cancellation probe. Callers that provide one must
  /// make it thread-safe, non-blocking, and non-throwing. Native backends may
  /// poll it at safe model-specific interruption points; an exception from a
  /// third-party probe is conservatively treated as cancellation.
  std::function<bool()> cancellationRequested;

  // Work-owned pure-result caches retain a completed, valid computation even
  // if its view became obsolete during Run. Queue/pre-Run cancellation still
  // applies. This does not request starting or continuing an unstarted batch.
  bool retainCompletedResultOnCancellation{false};

  [[nodiscard]] bool ShouldDiscardCompletedResult() const noexcept {
    return !retainCompletedResultOnCancellation && IsCancellationRequested();
  }

  [[nodiscard]] bool IsCancellationRequested() const noexcept {
    if (!cancellationRequested)
      return false;
    try {
      return cancellationRequested();
    } catch (...) {
      return true;
    }
  }
};

struct InferenceResponse final {
  std::string requestId;
  std::vector<Tensor> outputs;
};

/// Exact input metadata for preparing a lazy backend specialization. It owns
/// no tensor storage and never requests an inference or an output publication.
struct InferenceInputProfile final {
  std::string name;
  TensorElementType elementType{TensorElementType::Float32};
  std::vector<std::int64_t> shape;
};

struct InferenceProfile final {
  std::vector<InferenceInputProfile> inputs;
  [[nodiscard]] AiError Validate() const;
};

/// Model-specific implementations own ONNX Runtime, Core ML, Metal, or other
/// native objects. The shared AI contract only exchanges value tensors.
class IInferenceSession {
public:
  virtual ~IInferenceSession() = default;

  [[nodiscard]] virtual std::size_t MaximumBatchSize() const noexcept = 0;
  [[nodiscard]] virtual std::uint64_t ResidentBytes() const noexcept = 0;
  [[nodiscard]] virtual ActualComputePath ComputePath() const = 0;

  /// Called under the same exclusive session owner as RunBatch. Implementations
  /// may compile a shape specialization, but must not allocate input tensors or
  /// run inference. Unsupported backends reject the request explicitly.
  virtual AiError PrepareProfile(const InferenceProfile &,
                                 const std::function<bool()> &) {
    return AiError::Failure(AiErrorCode::BackendUnavailable,
                           "AI backend does not support input-profile preparation");
  }

  virtual Result<std::vector<InferenceResponse>>
  RunBatch(const std::vector<InferenceRequest> &requests) = 0;
};

[[nodiscard]] const char *
TensorElementTypeName(TensorElementType value) noexcept;
[[nodiscard]] std::size_t
TensorElementTypeBytes(TensorElementType value) noexcept;

} // namespace videocut::ai
