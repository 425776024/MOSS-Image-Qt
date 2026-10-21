#pragma once

#include "videocut/base/BaseApi.h"

#include <string_view>

namespace videocut::base {

VIDEOCUT_BASE_API void SetCurrentThreadName(std::string_view name);

/// Marks latency-sensitive work initiated by an explicit user action. This is
/// currently meaningful on Apple platforms and a no-op elsewhere.
VIDEOCUT_BASE_API void SetCurrentThreadUserInitiated();

/// Marks deferrable background work that must yield CPU scheduling priority to
/// interactive preview and export lanes. This is currently meaningful on
/// Apple platforms and a no-op elsewhere.
VIDEOCUT_BASE_API void SetCurrentThreadUtility();

} // namespace videocut::base
