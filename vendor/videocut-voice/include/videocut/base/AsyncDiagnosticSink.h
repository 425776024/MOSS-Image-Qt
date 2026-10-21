#pragma once

#include "videocut/base/BaseApi.h"

#include <cstddef>
#include <memory>
#include <string>

namespace videocut::base {

/// Bounded best-effort line sink for diagnostics emitted from latency-sensitive
/// paths. Append never waits for file I/O or for the consumer mutex.
class VIDEOCUT_BASE_API AsyncDiagnosticSink final {
public:
    explicit AsyncDiagnosticSink(
        std::string outputPath,
        std::string workerName = "VideoCutDiagnosticSink",
        std::size_t maximumPendingRecords = 8192);
    ~AsyncDiagnosticSink();

    AsyncDiagnosticSink(const AsyncDiagnosticSink&) = delete;
    AsyncDiagnosticSink& operator=(const AsyncDiagnosticSink&) = delete;

    void Append(std::string record) noexcept;
    void RequestFlush() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace videocut::base
