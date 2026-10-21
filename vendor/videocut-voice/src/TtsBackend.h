#pragma once

#include "videocut/ai/model/AiRuntime.h"
#include "videocut/ai/native/NativeSdk.h"

#include <memory>

namespace videocut::ai::native::detail {

[[nodiscard]] std::shared_ptr<model::IInferenceBackendFactory>
CreateTtsBackendFactory(ComputeBackend backend,
                        const NativeSdkOptions &options);

} // namespace videocut::ai::native::detail
