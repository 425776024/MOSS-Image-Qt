#pragma once

#include "videocut/base/AsyncDiagnosticSink.h"
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif
#if defined(__APPLE__)
#include <os/signpost.h>
#endif

namespace videocut::base {

// Opt-in per-process diagnostics. The existing bounded sink keeps file I/O
// away from render/UI workers. Stage names are compile-time diagnostic labels.
inline AsyncDiagnosticSink *StageTimingSink() noexcept {
  static auto sink = []() -> std::unique_ptr<AsyncDiagnosticSink> {
    try {
      const char *path = std::getenv("VIDEOCUT_PREVIEW_STAGE_TIMING_PATH");
      if (!path || !*path) return {};
#if defined(_WIN32)
      const auto process = _getpid();
#else
      const auto process = getpid();
#endif
      return std::make_unique<AsyncDiagnosticSink>(
          std::string(path) + "." + std::to_string(process) + ".ndjson",
          "PreviewStageTiming");
    } catch (...) { return {}; }
  }();
  return sink.get();
}

// An unset/invalid threshold preserves the existing opt-in trace behavior.
// Filtering happens before formatting or submitting to the bounded sink.
inline std::int64_t StageTimingMinimumUs() noexcept {
  static const auto minimumUs = []() -> std::int64_t {
    const char *value = std::getenv("VIDEOCUT_PREVIEW_STAGE_TIMING_MIN_US");
    if (!value || !*value) return 0;
    const char *end = value + std::char_traits<char>::length(value);
    std::int64_t parsed = 0;
    const auto result = std::from_chars(value, end, parsed);
    return result.ec == std::errc{} && result.ptr == end && parsed >= 0
        ? parsed : 0;
  }();
  return minimumUs;
}

#if defined(__APPLE__)
// Every stage is also an os_signpost interval (Instruments: Points of
// Interest, subsystem com.videocut). Signposts cost a flag check unless a
// recording is running; VIDEOCUT_SIGNPOSTS=0 turns them off entirely.
inline os_log_t StageSignpostLog() noexcept {
  static const os_log_t log = [] {
    const char *value = std::getenv("VIDEOCUT_SIGNPOSTS");
    return value && value[0] == '0' && value[1] == '\0'
        ? OS_LOG_DISABLED
        : os_log_create("com.videocut", OS_LOG_CATEGORY_POINTS_OF_INTEREST);
  }();
  return log;
}
#endif

class StageTiming final {
public:
  explicit StageTiming(const char *totalStage, std::int64_t ptsUs = -1) noexcept
      : sink_(StageTimingSink()), totalStage_(totalStage), ptsUs_(ptsUs) {
#if defined(__APPLE__)
    const os_log_t log = StageSignpostLog();
    if (os_signpost_enabled(log)) {
      signpost_ = os_signpost_id_generate(log);
      os_signpost_interval_begin(log, signpost_, "Stage", "%{public}s pts_us=%lld",
          totalStage_, static_cast<long long>(ptsUs_));
    }
#endif
    if (!sink_) return;
    start_ = previous_ = Clock::now();
    epochUs_ = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
  }
  ~StageTiming() {
#if defined(__APPLE__)
    if (signpost_ != OS_SIGNPOST_ID_NULL)
      os_signpost_interval_end(StageSignpostLog(), signpost_, "Stage", "%{public}s",
          totalStage_);
#endif
    if (!sink_) return;
    Emit(totalStage_, start_, Clock::now());
    if (emitted_) sink_->RequestFlush();
  }
  StageTiming(const StageTiming &) = delete;
  StageTiming &operator=(const StageTiming &) = delete;
  void Mark(const char *stage) noexcept {
#if defined(__APPLE__)
    if (signpost_ != OS_SIGNPOST_ID_NULL)
      os_signpost_event_emit(StageSignpostLog(), signpost_, "Mark", "%{public}s", stage);
#endif
    if (!sink_) return;
    const auto end = Clock::now();
    Emit(stage, previous_, end);
    previous_ = Clock::now();
  }
private:
  using Clock = std::chrono::steady_clock;
  void Emit(const char *stage, Clock::time_point begin, Clock::time_point end) noexcept {
    try {
      const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count();
      const auto minimumUs = StageTimingMinimumUs();
      if (minimumUs > 0 && elapsed <= minimumUs) return;
      const auto beginUs = epochUs_ + std::chrono::duration_cast<std::chrono::microseconds>(begin - start_).count();
      sink_->Append("{\"stage\":\"" + std::string(stage) + "\",\"start_us\":" +
          std::to_string(beginUs) + ",\"elapsed_us\":" + std::to_string(elapsed) +
          ",\"pts_us\":" + std::to_string(ptsUs_) + ",\"thread\":" +
          std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())) + "}");
      emitted_ = true;
    } catch (...) {}
  }
  AsyncDiagnosticSink *sink_;
  const char *totalStage_;
  std::int64_t ptsUs_;
  std::int64_t epochUs_{0};
  bool emitted_{false};
  Clock::time_point start_{}, previous_{};
#if defined(__APPLE__)
  os_signpost_id_t signpost_{OS_SIGNPOST_ID_NULL};
#endif
};

} // namespace videocut::base
