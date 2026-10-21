#pragma once

#include "videocut/base/Log.h"
#include <chrono>
#include <cstdlib>
#include <string>
#include <string_view>

namespace videocut::ai::model {

// Diagnostic-only. Startup metrics do not enable Core ML compute-plan profiling. Every
// phase shares steady-clock timestamps so startup on different lanes can be
// correlated without treating nested phase durations as additive.
class AiStartupTrace final {
public:
  AiStartupTrace(const char *stage, std::string_view identity = {}, bool selected = true)
      : stage_(stage), enabled_(selected && Enabled()) {
    if (!enabled_) return;
    try {
      // Product semantic keys can contain raw bytes. Keep diagnostic records
      // valid, single-line UTF-8 instead of writing the opaque key verbatim.
      constexpr char hex[] = "0123456789abcdef";
      for (const unsigned char byte : identity) {
        if (byte >= 0x20 && byte <= 0x7e && byte != '\\') identity_ += char(byte);
        else {
          identity_ += "\\x";
          identity_ += hex[byte >> 4];
          identity_ += hex[byte & 0xf];
        }
      }
      started_ = std::chrono::steady_clock::now();
      base::WriteLog(base::Severity::Info)
          << "[AI_STARTUP] event=begin stage=" << stage_ << " identity=" << identity_
          << " monotonic_us=" << Micros(started_) << std::endl;
    } catch (...) { enabled_ = false; }
  }
  ~AiStartupTrace() { Finish(); }
  AiStartupTrace(const AiStartupTrace &) = delete;
  AiStartupTrace &operator=(const AiStartupTrace &) = delete;

  void Finish() noexcept {
    if (!enabled_) return;
    enabled_ = false;
    try {
      const auto ended = std::chrono::steady_clock::now();
      base::WriteLog(base::Severity::Info)
          << "[AI_STARTUP] event=end stage=" << stage_ << " identity=" << identity_
          << " monotonic_us=" << Micros(ended)
          << " elapsed_us=" << Micros(ended) - Micros(started_) << std::endl;
    } catch (...) {}
  }

private:
  static bool Enabled() noexcept {
    static const bool enabled = [] {
      const auto on = [](const char *key) {
        const char *value = std::getenv(key);
        return value && std::string_view(value) == "1";
      };
      return on("VIDEOCUT_AI_STARTUP_METRICS") || on("VIDEOCUT_PREVIEW_TRACE")
          || on("VIDEOCUT_AI_COREML_TRACE");
    }();
    return enabled;
  }
  static std::int64_t Micros(std::chrono::steady_clock::time_point point) noexcept {
    return std::chrono::duration_cast<std::chrono::microseconds>(point.time_since_epoch()).count();
  }
  const char *stage_;
  bool enabled_;
  std::string identity_;
  std::chrono::steady_clock::time_point started_{};
};

} // namespace videocut::ai::model
