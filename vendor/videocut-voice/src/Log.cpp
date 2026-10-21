#include "videocut/base/Log.h"
#include "DiagnosticBudget.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <streambuf>
#include <unordered_map>
#include <utility>

namespace videocut::base {
namespace {

std::atomic<std::uint32_t> lineLimit{4096};
// Worker threads may still log while static destructors run at exit, so the
// gate and the file sink are never destroyed; the sink is flushed at exit.
std::mutex& OutputGate()
{
    static auto* gate = new std::mutex;
    return *gate;
}

std::ostream* MaintainedFileSink()
{
    static std::ofstream* const sink = []() -> std::ofstream* {
        const char* path = std::getenv("VIDEOCUT_NATIVE_LOG_PATH");
        if (!path || path[0] == '\0')
            return nullptr;
        // Keep one bounded previous session; appending forever makes a
        // process quota ineffective across repeated CLI/UI launches.
        std::error_code ignored;
        if (std::filesystem::exists(path, ignored)) {
            std::filesystem::remove(std::string(path) + ".1", ignored);
            std::filesystem::rename(path, std::string(path) + ".1", ignored);
        }
        auto file = std::make_unique<std::ofstream>(
            path, std::ios::out | std::ios::trunc);
        if (!file->is_open())
            return nullptr;
        std::atexit([] {
            std::lock_guard<std::mutex> lock(OutputGate());
            if (auto* sink = MaintainedFileSink())
                sink->flush();
        });
        return file.release();
    }();
    return sink;
}

const char* SeverityLabel(Severity severity) noexcept
{
    switch (severity) {
    case Severity::Trace: return "TRACE";
    case Severity::Debug: return "DEBUG";
    case Severity::Info: return "INFO";
    case Severity::Warning: return "WARN";
    case Severity::Error: return "ERROR";
    }
    return "UNKNOWN";
}

class DiscardBuffer final : public std::streambuf {
    int_type overflow(int_type value) override
    {
        return traits_type::not_eof(value);
    }
};

std::ostream& DiscardStream()
{
    // Even a discarded insertion mutates ostream formatting state. Keep the
    // stream and its buffer together, private to the calling thread.
    struct Sink {
        DiscardBuffer buffer;
        std::ostream stream{&buffer};
    };
    thread_local Sink sink;
    return sink.stream;
}

class ChannelBuffer final : public std::streambuf {
public:
    void Reset(std::shared_ptr<LogChannel::State> state, Severity severity)
    {
        if (!pending.empty())
            pubsync();
        destination = std::move(state);
        level = severity;
        omitted = 0;
    }

private:
    int_type overflow(int_type value) override;
    std::streamsize xsputn(const char* data, std::streamsize count) override;
    int sync() override;

    std::shared_ptr<LogChannel::State> destination;
    Severity level{Severity::Info};
    std::string pending;
    std::size_t omitted{0};
};

class ChannelStream final : public std::ostream {
public:
    ChannelStream() : std::ostream(&buffer) {}

    void Reset(std::shared_ptr<LogChannel::State> state, Severity severity)
    {
        buffer.Reset(std::move(state), severity);
        clear();
    }

private:
    ChannelBuffer buffer;
};

std::mutex channelGate;
std::unordered_map<std::string, std::unique_ptr<LogChannel>> channels;

} // namespace

struct LogChannel::State {
    explicit State(std::string channelName) : name(std::move(channelName)) {}

    bool Accepts(Severity severity) const noexcept
    {
        return static_cast<std::uint8_t>(severity)
            >= minimum.load(std::memory_order_relaxed);
    }

    void Emit(Severity severity, const std::string& text, std::size_t omitted)
    {
        if (!Accepts(severity))
            return;

        const auto now = std::chrono::system_clock::now();
        const auto milliseconds = std::chrono::duration_cast<
            std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
        const std::time_t raw = std::chrono::system_clock::to_time_t(now);
        std::tm local{};
#if defined(_WIN32)
        localtime_s(&local, &raw);
#else
        localtime_r(&raw, &local);
#endif

        std::ostringstream prefix;
        prefix << std::put_time(&local, "%H:%M:%S") << '.'
               << std::setfill('0') << std::setw(3) << milliseconds << " ["
               << SeverityLabel(severity) << ']';
        if (!name.empty())
            prefix << '[' << name << ']';
        prefix << ' ';

        const auto rendered = prefix.str() + text;
        if (severity == Severity::Error)
            diagnostics::ProcessBudget().RecordFirstError(rendered);
        if (!diagnostics::ProcessBudget().Admit(rendered.size() + 64))
            return;

        std::lock_guard<std::mutex> lock(OutputGate());
        const auto emit = [&](std::ostream& stream) {
            stream << prefix.str() << text;
            if (omitted != 0)
                stream << " [truncated " << omitted << " bytes]";
            if (severity == Severity::Error) stream.flush();
        };
        emit(std::clog);
        if (auto* sink = MaintainedFileSink())
            emit(*sink);
    }

    std::string name;
    std::atomic<std::uint8_t> minimum{
        static_cast<std::uint8_t>(Severity::Info)};
};

namespace {

ChannelBuffer::int_type ChannelBuffer::overflow(int_type value)
{
    if (traits_type::eq_int_type(value, traits_type::eof()))
        return traits_type::not_eof(value);
    const auto maximum = lineLimit.load(std::memory_order_relaxed);
    if (pending.size() < maximum)
        pending.push_back(traits_type::to_char_type(value));
    else
        ++omitted;
    return value;
}

std::streamsize ChannelBuffer::xsputn(
    const char* data,
    std::streamsize count)
{
    if (count <= 0)
        return 0;
    const auto maximum = lineLimit.load(std::memory_order_relaxed);
    const auto available =
        maximum > pending.size() ? maximum - pending.size() : 0;
    const auto accepted = std::min<std::size_t>(
        available, static_cast<std::size_t>(count));
    pending.append(data, accepted);
    omitted += static_cast<std::size_t>(count) - accepted;
    return count;
}

int ChannelBuffer::sync()
{
    if (destination && (!pending.empty() || omitted != 0))
        destination->Emit(level, pending, omitted);
    pending.clear();
    omitted = 0;
    return 0;
}

} // namespace

LogChannel::LogChannel(std::shared_ptr<State> state)
    : m_state(std::move(state))
{
}

std::ostream& LogChannel::Write(Severity severity)
{
    if (!m_state->Accepts(severity))
        return DiscardStream();
    thread_local ChannelStream stream;
    stream.Reset(m_state, severity);
    return stream;
}

void LogChannel::SetMinimum(Severity severity) noexcept
{
    m_state->minimum.store(
        static_cast<std::uint8_t>(severity),
        std::memory_order_relaxed);
}

Severity LogChannel::Minimum() const noexcept
{
    return static_cast<Severity>(
        m_state->minimum.load(std::memory_order_relaxed));
}

LogChannel& NamedLog(std::string_view name)
{
    // Never evict a destination: legacy callers retain references and raw
    // pointers. Dynamic identities belong to explicitly shared scoped channels.
    constexpr std::size_t maximumChannels = 256;
    constexpr std::size_t maximumNameBytes = 256;
    static LogChannel overflow(
        std::make_shared<LogChannel::State>("LogChannelOverflow"));
    if (name.size() > maximumNameBytes)
        return overflow;
    const std::string key(name);
    std::lock_guard<std::mutex> lock(channelGate);
    const auto found = channels.find(key);
    if (found != channels.end())
        return *found->second;
    if (channels.size() >= maximumChannels)
        return overflow;
    auto channel = std::unique_ptr<LogChannel>(
        new LogChannel(std::make_shared<LogChannel::State>(key)));
    auto* result = channel.get();
    channels.emplace(key, std::move(channel));
    return *result;
}

std::shared_ptr<LogChannel> MakeLogChannel(std::string_view name)
{
    return std::shared_ptr<LogChannel>(new LogChannel(
        std::make_shared<LogChannel::State>(std::string(name))));
}

LogChannel& ApplicationLog()
{
    return NamedLog({});
}

std::ostream& WriteLog(Severity severity)
{
    return ApplicationLog().Write(severity);
}

} // namespace videocut::base
