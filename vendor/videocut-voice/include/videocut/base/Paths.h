#pragma once

#include "videocut/base/BaseApi.h"

#include <string>
#include <vector>

namespace videocut::base {

VIDEOCUT_BASE_API std::string FileName(const std::string& path);
VIDEOCUT_BASE_API std::string FileStem(const std::string& path);
VIDEOCUT_BASE_API std::string FileExtension(const std::string& path);
VIDEOCUT_BASE_API std::string ParentDirectory(const std::string& path);
VIDEOCUT_BASE_API std::string CombinePath(
    const std::string& parent,
    const std::string& child);

VIDEOCUT_BASE_API bool PathExists(const std::string& path) noexcept;
VIDEOCUT_BASE_API bool DirectoryExists(const std::string& path) noexcept;
VIDEOCUT_BASE_API bool RegularFileExists(const std::string& path) noexcept;
VIDEOCUT_BASE_API bool MakeDirectory(
    const std::string& path,
    bool includeParents = true);
VIDEOCUT_BASE_API bool MovePath(
    const std::string& source,
    const std::string& destination);
VIDEOCUT_BASE_API bool RemoveFile(const std::string& path);

enum class NamePatternKind {
    Substring = 0,
    RegularExpression,
};

struct DirectoryQuery final {
    std::string root;
    std::string namePattern;
    NamePatternKind patternKind{NamePatternKind::Substring};
    bool caseSensitive{true};
    bool recursive{false};
};

struct DirectoryListing final {
    std::string root;
    std::vector<std::string> relativeFiles;
    std::string error;

    explicit operator bool() const noexcept { return error.empty(); }
};

/// Executes one bounded query and returns an immutable, sorted snapshot.
VIDEOCUT_BASE_API DirectoryListing ListDirectory(const DirectoryQuery& query);

} // namespace videocut::base
