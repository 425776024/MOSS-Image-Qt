#pragma once

#include "videocut/base/BaseApi.h"

#include <string>
#include <string_view>

namespace videocut::base {

/// Temporarily restores a macOS security-scoped bookmark, when one exists.
/// Other platforms and unsandboxed paths are harmless no-ops.
class VIDEOCUT_BASE_API SecurityScopedFileAccess final
{
public:
    SecurityScopedFileAccess(
        std::string_view path,
        std::string_view bookmarkKeyPrefix);
    ~SecurityScopedFileAccess();

    SecurityScopedFileAccess(const SecurityScopedFileAccess&) = delete;
    SecurityScopedFileAccess& operator=(const SecurityScopedFileAccess&) = delete;

    [[nodiscard]] const std::string& error() const noexcept
    {
        return m_error;
    }

private:
    [[maybe_unused]] void* m_url{nullptr};
    [[maybe_unused]] bool m_started{false};
    std::string m_error;
};

} // namespace videocut::base
