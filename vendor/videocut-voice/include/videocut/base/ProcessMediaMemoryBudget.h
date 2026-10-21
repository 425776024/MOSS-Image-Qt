#pragma once

#include "videocut/base/BaseApi.h"

#include <cstdint>
#include <string>

namespace videocut::base {

/// Conservative configured ceilings tracked across all workspaces linked into
/// the application's single host image. These reservations cover preview
/// caches and workspace buffer pools only; they are not a claim about the
/// process's complete resident set or an ABI across independently loaded
/// dynamic images.
struct ProcessMediaMemoryRequest final {
    std::uint64_t previewDecodeBytes{0};
    std::uint64_t previewPublishedBytes{0};
    std::uint64_t workspaceAllocationBytes{0};
};

struct ProcessMediaMemorySnapshot final {
    std::uint64_t activePreviewDecodeBytes{0};
    std::uint64_t activePreviewPublishedBytes{0};
    std::uint64_t activeWorkspaceAllocationBytes{0};
    std::uint64_t activeTrackedBytes{0};
    std::uint64_t previewDecodeHighWaterBytes{0};
    std::uint64_t previewPublishedHighWaterBytes{0};
    std::uint64_t workspaceAllocationHighWaterBytes{0};
    std::uint64_t trackedHighWaterBytes{0};
    std::uint64_t rejectedReservations{0};
    std::uint64_t accountingFaults{0};
    std::uint64_t maximumPreviewDecodeBytes{0};
    std::uint64_t maximumPreviewPublishedBytes{0};
    std::uint64_t maximumWorkspaceAllocationBytes{0};
};

/// One exactly-once host-image reservation. The authority remains resident for
/// that image's process lifetime and owns only scalar accounting; this holder
/// never owns a workspace or resource.
class VIDEOCUT_BASE_API ProcessMediaMemoryReservation final {
public:
    ProcessMediaMemoryReservation() noexcept = default;
    ~ProcessMediaMemoryReservation() noexcept;
    ProcessMediaMemoryReservation(
        ProcessMediaMemoryReservation&& other) noexcept;
    ProcessMediaMemoryReservation& operator=(
        ProcessMediaMemoryReservation&& other) noexcept;
    ProcessMediaMemoryReservation(
        const ProcessMediaMemoryReservation&) = delete;
    ProcessMediaMemoryReservation& operator=(
        const ProcessMediaMemoryReservation&) = delete;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] ProcessMediaMemoryRequest request() const noexcept;

    /// Atomically adds a nonnegative delta. Failure leaves this reservation
    /// unchanged.
    bool TryGrow(
        ProcessMediaMemoryRequest delta,
        std::string& error) noexcept;
    void Release() noexcept;

private:
    friend bool TryReserveProcessMediaMemory(
        ProcessMediaMemoryRequest,
        ProcessMediaMemoryReservation&,
        std::string&) noexcept;

    ProcessMediaMemoryReservation(
        void* authority,
        ProcessMediaMemoryRequest request) noexcept;

    void* m_authority{nullptr};
    ProcessMediaMemoryRequest m_request;
};

/// Acquires all requested categories atomically. A failed request does not
/// consume capacity and does not alter output.
VIDEOCUT_BASE_API bool TryReserveProcessMediaMemory(
    ProcessMediaMemoryRequest request,
    ProcessMediaMemoryReservation& output,
    std::string& error) noexcept;

[[nodiscard]] VIDEOCUT_BASE_API ProcessMediaMemorySnapshot
InspectProcessMediaMemory() noexcept;

/// Emits one low-volume tagged current/high-water snapshot. Callers use this
/// only at owner shutdown or another coarse lifecycle boundary.
VIDEOCUT_BASE_API void LogProcessMediaMemorySnapshot(
    const char* event) noexcept;

} // namespace videocut::base
