#pragma once

#include "videocut/base/BaseApi.h"

#include <cstdint>
#include <memory>
#include <ostream>
#include <string>
#include <string_view>

namespace videocut::base {

enum class Severity : std::uint8_t {
    Trace = 0,
    Debug,
    Info,
    Warning,
    Error,
};

/// A stable process log destination with one independently configurable
/// minimum severity. Formatting policy belongs to the sink implementation;
/// callers only choose a channel and severity.
class VIDEOCUT_BASE_API LogChannel final {
public:
    struct State;

    std::ostream& Write(Severity severity);

    void SetMinimum(Severity severity) noexcept;
    Severity Minimum() const noexcept;
private:
    explicit LogChannel(std::shared_ptr<State> state);
    std::shared_ptr<State> m_state;

    friend VIDEOCUT_BASE_API LogChannel& NamedLog(std::string_view name);
    friend VIDEOCUT_BASE_API std::shared_ptr<LogChannel> MakeLogChannel(
        std::string_view name);
};

/// Stable component destinations only: at most 256 names of at most 256 bytes
/// are interned. Excess names share a process-lifetime overflow destination.
/// Use MakeLogChannel for names containing instance or resource identities.
VIDEOCUT_BASE_API LogChannel& NamedLog(std::string_view name);
/// An independently owned destination, never inserted in the process registry.
/// Deferred work must share this handle for as long as it can write to it.
VIDEOCUT_BASE_API std::shared_ptr<LogChannel> MakeLogChannel(
    std::string_view name);
VIDEOCUT_BASE_API LogChannel& ApplicationLog();
VIDEOCUT_BASE_API std::ostream& WriteLog(Severity severity);

} // namespace videocut::base
