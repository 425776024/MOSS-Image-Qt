#include "NativeTensor.h"

#include <cstring>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace videocut::ai::native {
namespace {

template <typename T>
Tensor MakeTensor(std::string name, std::vector<std::int64_t> shape,
                  const T *values, const std::size_t count,
                  const TensorElementType type) {
  if (count != 0 && values == nullptr)
    throw std::invalid_argument("tensor values are null for a non-empty tensor");
  if (count > std::numeric_limits<std::size_t>::max() / sizeof(T))
    throw std::length_error("tensor byte count overflows");
  auto bytes = std::make_shared<std::vector<std::uint8_t>>(count * sizeof(T));
  if (count != 0)
    std::memcpy(bytes->data(), values, bytes->size());
  return Tensor{std::move(name), type, std::move(shape), std::move(bytes)};
}

template <typename T>
Result<std::vector<T>> ReadValues(const Tensor &tensor,
                                  const TensorElementType expected) {
  auto count = detail::ElementCount(tensor, expected);
  if (!count.ok())
    return Result<std::vector<T>>::Failure(count.error());
  std::vector<T> values(count.value());
  if (!values.empty())
    std::memcpy(values.data(), tensor.bytes->data(), tensor.bytes->size());
  return Result<std::vector<T>>::Success(std::move(values));
}

} // namespace

Tensor MakeFloatTensor(std::string name, std::vector<std::int64_t> shape,
                       const float *values, const std::size_t count) {
  return MakeTensor(std::move(name), std::move(shape), values, count,
                    TensorElementType::Float32);
}

Tensor MakeInt32Tensor(std::string name, std::vector<std::int64_t> shape,
                       const std::int32_t *values, const std::size_t count) {
  return MakeTensor(std::move(name), std::move(shape), values, count,
                    TensorElementType::Int32);
}

Tensor MakeInt64Tensor(std::string name, std::vector<std::int64_t> shape,
                       const std::int64_t *values, const std::size_t count) {
  return MakeTensor(std::move(name), std::move(shape), values, count,
                    TensorElementType::Int64);
}

Tensor MakeUInt8Tensor(std::string name, std::vector<std::int64_t> shape,
                       const std::uint8_t *values, const std::size_t count) {
  return MakeTensor(std::move(name), std::move(shape), values, count,
                    TensorElementType::UInt8);
}

Tensor MakeUtf8Tensor(std::string name, const std::string &value) {
  return MakeUInt8Tensor(std::move(name),
                         {static_cast<std::int64_t>(value.size())},
                         reinterpret_cast<const std::uint8_t *>(value.data()),
                         value.size());
}

Result<std::string> ReadUtf8Tensor(const Tensor &tensor) {
  auto count = detail::ElementCount(tensor, TensorElementType::UInt8);
  if (!count.ok())
    return Result<std::string>::Failure(count.error());
  return Result<std::string>::Success(std::string(
      reinterpret_cast<const char *>(tensor.bytes->data()), count.value()));
}

Result<std::vector<float>> ReadFloatTensor(const Tensor &tensor) {
  return ReadValues<float>(tensor, TensorElementType::Float32);
}

Result<std::vector<std::int32_t>> ReadInt32Tensor(const Tensor &tensor) {
  return ReadValues<std::int32_t>(tensor, TensorElementType::Int32);
}

Result<std::vector<std::int64_t>> ReadInt64Tensor(const Tensor &tensor) {
  return ReadValues<std::int64_t>(tensor, TensorElementType::Int64);
}

Result<std::vector<std::uint8_t>> ReadUInt8Tensor(const Tensor &tensor) {
  return ReadValues<std::uint8_t>(tensor, TensorElementType::UInt8);
}

namespace detail {

const Tensor *FindTensor(const InferenceRequest &request,
                         const char *name) noexcept {
  for (const Tensor &tensor : request.inputs) {
    if (tensor.name == name)
      return &tensor;
  }
  return nullptr;
}

Result<const Tensor *> RequireTensor(const InferenceRequest &request,
                                     const char *name,
                                     const TensorElementType type) {
  const Tensor *tensor = FindTensor(request, name);
  if (tensor == nullptr) {
    return Result<const Tensor *>::Failure(
        AiErrorCode::InvalidArgument,
        std::string("required input tensor is missing: ") + name);
  }
  const AiError validation = tensor->Validate();
  if (!validation.ok())
    return Result<const Tensor *>::Failure(validation);
  if (tensor->elementType != type) {
    return Result<const Tensor *>::Failure(
        AiErrorCode::TensorInvalid,
        std::string("input tensor has the wrong element type: ") + name);
  }
  return Result<const Tensor *>::Success(tensor);
}

Result<std::size_t> ElementCount(const Tensor &tensor,
                                 const TensorElementType expected) {
  const AiError validation = tensor.Validate();
  if (!validation.ok())
    return Result<std::size_t>::Failure(validation);
  if (tensor.elementType != expected) {
    return Result<std::size_t>::Failure(
        AiErrorCode::TensorInvalid, "tensor element type does not match");
  }
  return Result<std::size_t>::Success(
      tensor.bytes->size() / TensorElementTypeBytes(expected));
}

Result<std::int32_t> ReadScalarInt32(const InferenceRequest &request,
                                     const char *name) {
  auto tensor = RequireTensor(request, name, TensorElementType::Int32);
  if (!tensor.ok())
    return Result<std::int32_t>::Failure(tensor.error());
  auto count = ElementCount(*tensor.value(), TensorElementType::Int32);
  if (!count.ok() || count.value() != 1) {
    return Result<std::int32_t>::Failure(
        AiErrorCode::TensorInvalid,
        std::string("input must be one int32 value: ") + name);
  }
  std::int32_t value = 0;
  std::memcpy(&value, tensor.value()->bytes->data(), sizeof(value));
  return Result<std::int32_t>::Success(value);
}

Result<std::int64_t>
ReadOptionalScalarInt64(const InferenceRequest &request, const char *name,
                        const std::int64_t fallback) {
  const Tensor *tensor = FindTensor(request, name);
  if (tensor == nullptr)
    return Result<std::int64_t>::Success(fallback);
  auto count = ElementCount(*tensor, TensorElementType::Int64);
  if (!count.ok() || count.value() != 1) {
    return Result<std::int64_t>::Failure(
        AiErrorCode::TensorInvalid,
        std::string("input must be one int64 value: ") + name);
  }
  std::int64_t value = 0;
  std::memcpy(&value, tensor->bytes->data(), sizeof(value));
  return Result<std::int64_t>::Success(value);
}

Result<std::string> ReadRequiredUtf8(const InferenceRequest &request,
                                     const char *name) {
  auto tensor = RequireTensor(request, name, TensorElementType::UInt8);
  if (!tensor.ok())
    return Result<std::string>::Failure(tensor.error());
  return ReadUtf8Tensor(*tensor.value());
}

Result<std::vector<float>>
ReadRequiredFloat(const InferenceRequest &request, const char *name) {
  auto tensor = RequireTensor(request, name, TensorElementType::Float32);
  if (!tensor.ok())
    return Result<std::vector<float>>::Failure(tensor.error());
  return ReadFloatTensor(*tensor.value());
}

Result<std::vector<std::uint8_t>>
ReadRequiredUInt8(const InferenceRequest &request, const char *name) {
  auto tensor = RequireTensor(request, name, TensorElementType::UInt8);
  if (!tensor.ok())
    return Result<std::vector<std::uint8_t>>::Failure(tensor.error());
  return Result<std::vector<std::uint8_t>>::Success(
      *tensor.value()->bytes);
}

Tensor MakeFloatOutput(std::string name, std::vector<std::int64_t> shape,
                       std::vector<float> values) {
  return MakeFloatTensor(std::move(name), std::move(shape), values.data(),
                         values.size());
}

Tensor MakeInt64Output(std::string name, std::vector<std::int64_t> shape,
                       std::vector<std::int64_t> values) {
  return MakeInt64Tensor(std::move(name), std::move(shape), values.data(),
                         values.size());
}

Tensor MakeInt32Output(std::string name, std::vector<std::int64_t> shape,
                       std::vector<std::int32_t> values) {
  return MakeInt32Tensor(std::move(name), std::move(shape), values.data(),
                         values.size());
}

Tensor MakeUtf8Output(std::string name, std::string value) {
  return MakeUtf8Tensor(std::move(name), value);
}

} // namespace detail
} // namespace videocut::ai::native
