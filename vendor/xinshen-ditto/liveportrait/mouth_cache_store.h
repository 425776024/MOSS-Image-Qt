#pragma once
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <regex>
#include <stdexcept>
#include <vector>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

// Shared by the native worker and Settings. Never manages media or model files.
namespace ditto::mouth_cache {
namespace fs = std::filesystem;
inline constexpr uintmax_t limitBytes = 128ULL * 1024 * 1024;
class Lock {
    int fd = -1;
public:
    explicit Lock(const fs::path &directory) {
        fs::create_directories(directory);
        if (fs::is_symlink(fs::symlink_status(directory))) throw std::runtime_error("cache directory is a symlink");
        fd = ::open((directory / ".lock").c_str(), O_CREAT | O_RDWR | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (fd < 0) throw std::runtime_error("cannot open cache lock");
        if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
            ::close(fd); fd = -1; throw std::runtime_error("cache is busy");
        }
    }
    Lock(const Lock &) = delete;
    Lock &operator=(const Lock &) = delete;
    ~Lock() { if (fd >= 0) { ::flock(fd, LOCK_UN); ::close(fd); } }
};
struct Entry { fs::path path; uintmax_t bytes; fs::file_time_type used; bool partial; };
inline std::vector<Entry> entries(const fs::path &directory) {
    static const std::regex owned("[0-9a-f]{64}(-native-silence[0-9]+-v[0-9]+)?\\.json(\\.[0-9]+\\.[0-9]+\\.partial)?");
    std::vector<Entry> result;
    for (const auto &f : fs::directory_iterator(directory)) {
        if (!fs::is_regular_file(f.symlink_status()) || !std::regex_match(f.path().filename().string(), owned)) continue;
        result.push_back({f.path(), f.file_size(), f.last_write_time(), f.path().extension()==".partial"});
    }
    return result;
}
// Call while holding Lock. Reads touch mtime explicitly; filesystem atime is not relied on.
inline uintmax_t prune(const fs::path &directory, uintmax_t limit = limitBytes, bool clear = false) {
    auto files = entries(directory);
    std::sort(files.begin(), files.end(), [](const Entry &a, const Entry &b) {
        return a.used == b.used ? a.path < b.path : a.used < b.used;
    });
    uintmax_t total = 0;
    for (const auto &f : files) total += f.bytes;
    // Writers hold the same lock throughout their atomic write. Any partial
    // found here was abandoned by a crashed/killed writer and can be reclaimed.
    for (const auto &f : files) if (clear || f.partial || f.bytes > limit) {
        if (fs::remove(f.path)) total -= f.bytes;
    }
    if (!clear) for (const auto &f : files) {
        if (total <= limit) break;
        if (!f.partial && f.bytes <= limit && fs::remove(f.path)) total -= f.bytes;
    }
    return total;
}
inline void touch(const fs::path &path) { fs::last_write_time(path, fs::file_time_type::clock::now()); }
}
