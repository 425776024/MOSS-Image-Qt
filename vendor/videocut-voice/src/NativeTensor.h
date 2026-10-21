#pragma once

#include "videocut/ai/native/NativeSdk.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace videocut::ai::native::detail {

[[nodiscard]] const Tensor *FindTensor(const InferenceRequest &request,
                                       const char *name) noexcept;
[[nodiscard]] Result<const Tensor *>
RequireTensor(const InferenceRequest &request, const char *name,
              TensorElementType type);
[[nodiscard]] Result<std::int32_t>
ReadScalarInt32(const InferenceRequest &request, const char *name);
[[nodiscard]] Result<std::int64_t>
ReadOptionalScalarInt64(const InferenceRequest &request, const char *name,
                        std::int64_t fallback);
[[nodiscard]] Result<std::string>
ReadRequiredUtf8(const InferenceRequest &request, const char *name);
[[nodiscard]] Result<std::vector<float>>
ReadRequiredFloat(const InferenceRequest &request, const char *name);
[[nodiscard]] Result<std::vector<std::uint8_t>>
ReadRequiredUInt8(const InferenceRequest &request, const char *name);

[[nodiscard]] Tensor MakeFloatOutput(std::string name,
                                     std::vector<std::int64_t> shape,
                                     std::vector<float> values);
[[nodiscard]] Tensor MakeInt64Output(std::string name,
                                     std::vector<std::int64_t> shape,
                                     std::vector<std::int64_t> values);
[[nodiscard]] Tensor MakeInt32Output(std::string name,
                                     std::vector<std::int64_t> shape,
                                     std::vector<std::int32_t> values);
[[nodiscard]] Tensor MakeUtf8Output(std::string name, std::string value);

[[nodiscard]] Result<std::size_t>
ElementCount(const Tensor &tensor, TensorElementType expected);

} // namespace videocut::ai::native::detail
