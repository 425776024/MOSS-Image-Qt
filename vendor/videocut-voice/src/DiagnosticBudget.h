#pragma once

#include <atomic>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <string>

namespace videocut::base::diagnostics {

// One process-wide quota shared by the native logger and async trace sinks.
// These are raw diagnostics, never authoritative operation receipts/metrics.
inline std::size_t Limit(const char* key, std::size_t fallback) {
    const char* text = std::getenv(key);
    if (!text || !*text) return fallback;
    char* end = nullptr;
    const auto value = std::strtoull(text, &end, 10);
    return end && *end == '\0' && value > 0 && value <= 64U * 1024U * 1024U
        ? static_cast<std::size_t>(value) : fallback;
}

struct Budget final {
    const std::size_t maxRecords = Limit("VIDEOCUT_LOG_MAX_RECORDS", 10000);
    const std::size_t maxBytes = Limit("VIDEOCUT_LOG_MAX_BYTES", 8U * 1024U * 1024U);
    std::mutex mutex;
    std::size_t records = 0, bytes = 0, suppressed = 0;
    bool reported = false;
    std::atomic<bool> firstError{false};

    bool Admit(std::size_t size) {
        std::lock_guard<std::mutex> lock(mutex);
        if (records >= maxRecords || size > 4096 || bytes + size > maxBytes) {
            ++suppressed;
            if (!reported) {
                reported = true;
                const char* path = std::getenv("VIDEOCUT_NATIVE_LOG_PATH");
                if (path && *path) {
                    std::ofstream summary(std::string(path) + ".budget.json", std::ios::trunc);
                    summary << "{\"budgetExceeded\":true,\"records\":" << records
                            << ",\"bytes\":" << bytes << ",\"suppressedAtLeast\":1}\n";
                }
            }
            return false;
        }
        ++records;
        bytes += size;
        return true;
    }

    void RecordFirstError(const std::string& text) {
        if (firstError.exchange(true)) return;
        const char* path = std::getenv("VIDEOCUT_NATIVE_LOG_PATH");
        if (!path || !*path) return;
        std::ofstream error(std::string(path) + ".first-error.log", std::ios::trunc);
        error << text.substr(0, 4096) << '\n';
    }
};

inline Budget& ProcessBudget() {
    static Budget budget;
    return budget;
}
} // namespace videocut::base::diagnostics
