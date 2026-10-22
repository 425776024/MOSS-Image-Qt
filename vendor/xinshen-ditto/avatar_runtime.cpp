#include "avatar_runtime.h"
#include "media_io.h"
#ifdef __APPLE__
#include <CommonCrypto/CommonDigest.h>
#else
#include <openssl/evp.h>
#endif
#include <limits>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <csignal>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>

namespace avatar {
namespace {
static_assert(std::atomic<bool>::is_always_lock_free, "Signal cancellation requires a lock-free flag");
std::atomic<bool> cancelled{false};
std::mutex progressMutex;
void cancel(int) { cancelled.store(true, std::memory_order_relaxed); }
}
void installCancellation() { std::signal(SIGTERM, cancel); std::signal(SIGINT, cancel); }
void checkCancelled() { if (cancelled.load(std::memory_order_relaxed)) throw std::runtime_error("任务已取消"); }
void progress(const std::string& stage, int64_t done, int64_t total) {
  // Model loading and audio motion may report progress concurrently.
  std::lock_guard<std::mutex> lock(progressMutex);
  checkCancelled();
  std::cout << Json{{"event", "progress"}, {"stage", stage}, {"done", done},
                    {"total", total}}.dump() << '\n' << std::flush;
}
Json readJson(const std::filesystem::path& path) {
  std::ifstream in(path);
  if (!in) throw std::runtime_error("无法读取 " + path.string());
  return Json::parse(in);
}
void writeJson(const std::filesystem::path& path, const Json& value) {
  const auto temp = path.string() + ".partial";
  { std::ofstream out(temp); out << value.dump(2) << '\n'; out.flush();
    if (!out) throw std::runtime_error("无法写入 " + path.string()); }
  checkCancelled();
  std::filesystem::rename(temp, path);
}
std::filesystem::path member(const std::filesystem::path& root, const std::string& relative) {
  const std::filesystem::path rel(relative);
  if (rel.empty() || rel.is_absolute()) throw std::runtime_error("形象资源路径无效");
  for (const auto& part : rel) if (part == "..") throw std::runtime_error("形象资源路径越界");
  const auto base = std::filesystem::canonical(root);
  const auto result = std::filesystem::canonical(base / rel);
  auto a = base.begin(), b = result.begin();
  for (; a != base.end(); ++a, ++b)
    if (b == result.end() || *a != *b) throw std::runtime_error("形象资源链接越界");
  return result;
}
namespace {
class Sha256 {
public:
  Sha256() {
#ifdef __APPLE__
    if (CC_SHA256_Init(&context_) != 1) throw std::runtime_error("SHA256 初始化失败");
#else
    if (!context_ || EVP_DigestInit_ex(context_.get(), EVP_sha256(), nullptr) != 1)
      throw std::runtime_error("SHA256 初始化失败");
#endif
  }
  void update(const void* bytes, size_t size) {
#ifdef __APPLE__
    const auto* cursor = static_cast<const unsigned char*>(bytes);
    while (size) {
      const auto chunk = static_cast<CC_LONG>(std::min(size, size_t(std::numeric_limits<CC_LONG>::max())));
      if (CC_SHA256_Update(&context_, cursor, chunk) != 1) throw std::runtime_error("SHA256 校验失败");
      cursor += chunk; size -= chunk;
    }
#else
    if (EVP_DigestUpdate(context_.get(), bytes, size) != 1) throw std::runtime_error("SHA256 校验失败");
#endif
  }
  std::string finish() {
    unsigned char digest[32];
#ifdef __APPLE__
    if (CC_SHA256_Final(digest, &context_) != 1) throw std::runtime_error("SHA256 失败");
#else
    unsigned int length = 0;
    if (EVP_DigestFinal_ex(context_.get(), digest, &length) != 1 || length != sizeof(digest))
      throw std::runtime_error("SHA256 失败");
#endif
    std::ostringstream result;
    for (auto byte : digest) result << std::hex << std::setw(2) << std::setfill('0') << int(byte);
    return result.str();
  }
private:
#ifdef __APPLE__
  CC_SHA256_CTX context_{};
#else
  std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context_{EVP_MD_CTX_new(), EVP_MD_CTX_free};
#endif
};
}
std::string sha256(const void* bytes, size_t size) {
  Sha256 hash; hash.update(bytes, size); return hash.finish();
}
std::string sha256(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error("无法校验 " + path.string());
  Sha256 hash;
  std::array<char, 1024 * 256> buffer{};
  while (in) {
    checkCancelled(); in.read(buffer.data(), buffer.size());
    hash.update(buffer.data(), static_cast<size_t>(in.gcount()));
  }
  if (!in.eof()) throw std::runtime_error("资源读取失败");
  return hash.finish();
}
AudioPlan readAudioPlan(const std::filesystem::path& path) {
  const auto plan = readJson(path);
  if (plan.at("version") != 1) throw std::runtime_error("不支持的语音计划版本");
  const auto total = plan.at("totalSamples").get<int64_t>();
  if (total <= 0 || total > 16000LL * 600) throw std::runtime_error("口播时长必须在十分钟以内");
  AudioPlan result;
  result.samples.assign(total, 0); result.covered.assign(total, 0);
  for (const auto& segment : plan.at("segments")) {
    checkCancelled();
    const auto source = decodeAudio16k(segment.at("path").get<std::string>());
    const auto begin = segment.at("inputStart").get<int64_t>();
    const auto output = segment.at("outputStart").get<int64_t>();
    const auto count = segment.at("sampleCount").get<int64_t>();
    const auto gain = segment.value("gain", 1.0f);
    if (begin < 0 || output < 0 || count <= 0 || count > total || output > total - count ||
        count > int64_t(source.size()) || begin > int64_t(source.size()) - count ||
        !std::isfinite(gain) || gain < 0) throw std::runtime_error("语音片段范围无效");
    for (int64_t i = 0; i < count; ++i) {
      result.samples[output+i] += source[begin+i] * gain;
      if (gain > 0) result.covered[output+i] = 1;
    }
  }
  for (auto& sample : result.samples) {
    if (!std::isfinite(sample)) throw std::runtime_error("语音包含无效采样值");
    sample = std::clamp(sample, -1.0f, 1.0f);
  }
  return result;
}
}
