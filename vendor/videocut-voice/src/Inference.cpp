#include "videocut/ai/Inference.h"

#include <limits>
#include <unordered_set>

namespace videocut::ai {

const char *TensorElementTypeName(const TensorElementType value) noexcept {
  switch (value) {
  case TensorElementType::Float32:
    return "float32";
  case TensorElementType::Float16:
    return "float16";
  case TensorElementType::Int64:
    return "int64";
  case TensorElementType::Int32:
    return "int32";
  case TensorElementType::UInt8:
    return "uint8";
  case TensorElementType::Bool:
    return "bool";
  }
  return "unknown";
}

std::size_t TensorElementTypeBytes(const TensorElementType value) noexcept {
  switch (value) {
  case TensorElementType::Float32:
  case TensorElementType::Int32:
    return 4;
  case TensorElementType::Float16:
    return 2;
  case TensorElementType::Int64:
    return 8;
  case TensorElementType::UInt8:
  case TensorElementType::Bool:
    return 1;
  }
  return 0;
}

AiError InferenceProfile::Validate() const {
  if (inputs.empty() || inputs.size() > 32U)
    return AiError::Failure(AiErrorCode::InvalidArgument,
                           "AI input profile must have 1..32 inputs");
  std::unordered_set<std::string> names;
  for (const auto &input : inputs) {
    const auto bytes = TensorElementTypeBytes(input.elementType);
    if (input.name.empty() || input.name.size() > 256U ||
        !names.insert(input.name).second || bytes == 0U ||
        input.shape.empty() || input.shape.size() > 16U)
      return AiError::Failure(AiErrorCode::InvalidArgument,
                             "AI input profile metadata is invalid");
    std::uint64_t count = bytes;
    for (const auto dimension : input.shape) {
      if (dimension <= 0 || count > std::numeric_limits<std::uint64_t>::max() /
                                       static_cast<std::uint64_t>(dimension))
        return AiError::Failure(AiErrorCode::InvalidArgument,
                               "AI input profile extent is invalid or overflows");
      count *= static_cast<std::uint64_t>(dimension);
    }
  }
  return AiError::None();
}

AiError Tensor::Validate() const {
  if (name.empty()) {
    return AiError::Failure(AiErrorCode::TensorInvalid,
                            "tensor name must not be empty");
  }
  if (!bytes) {
    return AiError::Failure(AiErrorCode::TensorInvalid,
                            "tensor bytes must not be null");
  }
  const std::size_t elementBytes = TensorElementTypeBytes(elementType);
  if (elementBytes == 0) {
    return AiError::Failure(AiErrorCode::TensorInvalid,
                            "tensor element type is unsupported");
  }

  std::size_t elementCount = 1;
  for (const std::int64_t dimension : shape) {
    if (dimension < 0) {
      return AiError::Failure(AiErrorCode::TensorInvalid,
                              "tensor dimensions must not be negative");
    }
    const auto unsignedDimension = static_cast<std::size_t>(dimension);
    if (unsignedDimension == 0) {
      elementCount = 0;
      continue;
    }
    if (elementCount == 0)
      continue;
    if (elementCount >
        std::numeric_limits<std::size_t>::max() / unsignedDimension) {
      return AiError::Failure(AiErrorCode::TensorInvalid,
                              "tensor element count overflows size_t");
    }
    elementCount *= unsignedDimension;
  }
  if (elementCount > std::numeric_limits<std::size_t>::max() / elementBytes) {
    return AiError::Failure(AiErrorCode::TensorInvalid,
                            "tensor byte count overflows size_t");
  }
  const std::size_t expectedBytes = elementCount * elementBytes;
  if (bytes->size() != expectedBytes) {
    return AiError::Failure(
        AiErrorCode::TensorInvalid,
        "tensor byte count does not match its element type and shape");
  }
  return AiError::None();
}

} // namespace videocut::ai
