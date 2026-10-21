#pragma once

#include "videocut/base/BaseApi.h"

#include <cstdint>

namespace videocut::base {

/// Process-wide GPU execution time from Metal's command-buffer timestamps
/// (GPUStartTime/GPUEndTime). Recording is a few relaxed atomics from the
/// completion handler; readers take deltas between two snapshots.
/// Coverage is the SDK surface-transform, Metal-backend and Skia submission
/// hooks, not Qt QSG or CoreML internals. Concurrent queues' durations add;
/// this measures recorded work, not wall-clock GPU utilization.
struct GpuTimingTotals final {
    std::uint64_t commandBuffers{0};
    std::uint64_t gpuMicroseconds{0};
    std::uint64_t maximumMicroseconds{0};
    // Cumulative completed buffers whose unrounded Metal duration is >=12 ms.
    // Unlike the lifetime maximum, its delta identifies violations in a window.
    std::uint64_t over12msCommandBuffers{0};
};

VIDEOCUT_BASE_API void RecordGpuCommandBuffer(
    double gpuStartSeconds, double gpuEndSeconds) noexcept;
[[nodiscard]] VIDEOCUT_BASE_API GpuTimingTotals GpuTimingSnapshot() noexcept;

} // namespace videocut::base
