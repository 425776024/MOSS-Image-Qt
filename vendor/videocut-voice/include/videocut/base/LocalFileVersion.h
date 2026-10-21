#pragma once

#include "videocut/base/BaseApi.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace videocut::base {

struct LocalFileVersionOptions final
{
    bool requireAbsolutePath{false};
    bool rejectLeafSymlink{false};
    bool requireNonEmpty{false};
};

/// Stable identity and version metadata captured from one OS file snapshot.
/// Paths which cannot provide every field fail closed.
struct LocalFileVersion final
{
    std::string canonicalPath;
    std::uint64_t size{0};
    std::uint64_t storageId{0};
    std::uint64_t fileId{0};
    std::int64_t modifiedSeconds{0};
    std::int64_t modifiedNanoseconds{0};
    std::int64_t changedSeconds{0};
    std::int64_t changedNanoseconds{0};
};

VIDEOCUT_BASE_API bool operator==(
    const LocalFileVersion& left,
    const LocalFileVersion& right) noexcept;

VIDEOCUT_BASE_API bool InspectLocalFileVersion(
    std::string_view path,
    const LocalFileVersionOptions& options,
    LocalFileVersion& version) noexcept;

} // namespace videocut::base
