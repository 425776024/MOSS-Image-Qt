#pragma once

#include "videocut/base/BaseApi.h"

#include <cstdint>

namespace videocut::base {

/// Memory class the process sizes its caches and budgets for. Budgets in the
/// code are written for a 16 GB machine (Standard); an entry M1/M2 Air with
/// 8 GB of unified memory shared by CPU and GPU runs the Compact tier.
enum class MemoryTier : std::uint8_t {
    Compact,  // <= 8 GB
    Standard, // <= 16 GB
    Large,    // > 16 GB
};

/// Decided once per process from the physical memory.
struct MachineProfile final {
    std::uint64_t physicalMemoryBytes{0};
    std::uint32_t performanceCores{0};
    std::uint32_t efficiencyCores{0};
    std::uint32_t logicalCores{0};
    MemoryTier memoryTier{MemoryTier::Standard};
};

[[nodiscard]] VIDEOCUT_BASE_API const MachineProfile&
CurrentMachineProfile() noexcept;
[[nodiscard]] VIDEOCUT_BASE_API const char* MemoryTierName(
    MemoryTier tier) noexcept;

/// A byte budget authored for the Standard tier, sized for the current tier:
/// half on Compact, unchanged otherwise.
[[nodiscard]] VIDEOCUT_BASE_API std::uint64_t ScaleForMemoryTier(
    std::uint64_t standardBytes) noexcept;
/// Chooses per tier explicitly when halving is not the right shape.
template <typename T>
[[nodiscard]] T ForMemoryTier(T compact, T standard) noexcept
{
    return CurrentMachineProfile().memoryTier == MemoryTier::Compact
        ? compact : standard;
}

/// FFmpeg software decoder threads for one stream: the performance cores, at
/// most four. libavcodec otherwise decodes on a single thread.
[[nodiscard]] VIDEOCUT_BASE_API std::uint32_t
SoftwareDecodeThreadCount() noexcept;

/// The process's physical footprint, the figure jetsam and Activity Monitor
/// use; 0 when unavailable.
[[nodiscard]] VIDEOCUT_BASE_API std::uint64_t
CurrentPhysicalFootprintBytes() noexcept;

enum class ThermalLevel : std::uint8_t {
    Nominal,
    Fair,
    Serious,
    Critical,
};

/// Thermal state and Low Power Mode, tracked from the system notifications.
/// Debug builds only: VIDEOCUT_SIMULATE_THERMAL=fair|serious|critical and
/// VIDEOCUT_SIMULATE_LOW_POWER=1 force a state for testing.
struct PowerConditions final {
    ThermalLevel thermal{ThermalLevel::Nominal};
    bool lowPowerMode{false};
    /// Changes whenever thermal or Low Power Mode changes.
    std::uint32_t generation{0};
};

[[nodiscard]] VIDEOCUT_BASE_API PowerConditions
CurrentPowerConditions() noexcept;
/// True while the machine should shed optional work: thermal Serious or
/// worse, or Low Power Mode. Cheap enough to call once per frame.
[[nodiscard]] VIDEOCUT_BASE_API bool ShouldReduceWork() noexcept;
[[nodiscard]] VIDEOCUT_BASE_API const char* ThermalLevelName(
    ThermalLevel level) noexcept;

enum class MemoryPressureLevel : std::uint8_t {
    Normal,
    Warning,
    Critical,
};

/// The kernel's memory pressure verdict (what jetsam acts on), read at most
/// once a second so caches can size themselves from it on every use.
[[nodiscard]] VIDEOCUT_BASE_API MemoryPressureLevel
CurrentMemoryPressure() noexcept;
[[nodiscard]] VIDEOCUT_BASE_API const char* MemoryPressureLevelName(
    MemoryPressureLevel level) noexcept;

} // namespace videocut::base
