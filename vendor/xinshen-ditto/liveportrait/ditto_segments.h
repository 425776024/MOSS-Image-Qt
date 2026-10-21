#pragma once
#include "../avatar_runtime.h"
#include "../media_io.h"
#include <algorithm>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <vector>

namespace ditto {
// One writer owner rotates completed media segments without reloading inference
// models. Only atomically published records are eligible for the app's cache.
class RenderSegments {
 public:
  using J = avatar::Json;
  using Path = std::filesystem::path;
  struct Range { int64_t first, count, begin; };
  RenderSegments(const J& plan, int64_t total, int64_t first, int64_t count,
                 const Path& output, const Path& temporary,
                 const std::vector<float>& audio, int width, int height,
                 int fps, int den, bool lossless)
      : output_(output), temporary_(temporary), audio_(audio), width_(width),
        height_(height), fps_(fps), den_(den), lossless_(lossless),
        segmented_(plan.contains("segmentDirectory")),
        includeAudio_(plan.value("includeAudio", false)) {
    if (segmented_) {
      check(!includeAudio_, "segmented preview uses the timeline audio track");
      directory_ = std::filesystem::absolute(plan.at("segmentDirectory").get<std::string>());
      const int64_t size = plan.value("segmentFrames", int64_t(0));
      check(size > 0, "segmentFrames must be positive");
      if (plan.contains("segmentRanges")) {
        const auto& ranges = plan.at("segmentRanges");
        check(ranges.is_array() && !ranges.empty(), "empty segmentRanges");
        for (const auto& range : ranges) {
          const auto a = range.at("firstFrame").get<int64_t>();
          const auto n = range.at("frameCount").get<int64_t>();
          check(n <= size, "segment range exceeds segmentFrames");
          append(a, n, total);
        }
      } else {
        for (int64_t offset = 0; offset < count; offset += size)
          append(first + offset, std::min(size, count - offset), total);
      }
      auto sorted = ranges_;
      std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
      for (size_t i = 1; i < sorted.size(); ++i)
        check(sorted[i-1].first + sorted[i-1].count <= sorted[i].first,
              "overlapping segment ranges");
      check(!std::filesystem::exists(directory_) ||
                (std::filesystem::is_directory(directory_) && std::filesystem::is_empty(directory_)),
            "segment directory must be new or empty");
      std::filesystem::create_directories(directory_);
    } else {
      check(!plan.contains("segmentRanges"), "segmentRanges requires segmentDirectory");
      append(first, count, total);
    }
    open();
  }
  ~RenderSegments() {
    writer_.reset();
    std::error_code ignored;
    if (!activeTemporary_.empty()) std::filesystem::remove(activeTemporary_, ignored);
    if (!recordTemporary_.empty()) std::filesystem::remove(recordTemporary_, ignored);
  }
  int64_t count() const { return count_; }
  int64_t frameAt(size_t index) const {
    check(index < size_t(count_), "render schedule index");
    auto it = std::upper_bound(ranges_.begin(), ranges_.end(), int64_t(index),
        [](int64_t i, const Range& r) { return i < r.begin; });
    --it;
    return it->first + int64_t(index) - it->begin;
  }
  bool segmented() const { return segmented_; }
  const J& records() const { return records_; }
  Path path() const {
    return segmented_ && !records_.empty() ? Path(records_.front().at("path").get<std::string>()) : output_;
  }
  void write(const Image& image) {
    check(written_ < count_ && writer_ != nullptr, "segment writer frame count");
    writer_->write(image);
    ++written_;
    const auto& range = ranges_[current_];
    if (written_ == range.begin + range.count) {
      close();
      if (++current_ < ranges_.size()) open();
    }
  }
  void finish() {
    check(written_ == count_ && !writer_, "incomplete render segments");
  }
  void publish() {
    finish();
    if (!segmented_) {
      avatar::checkCancelled();
      std::filesystem::rename(activeTemporary_, output_);
      activeTemporary_.clear();
    }
  }
 private:
  static void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(std::string("Ditto: ") + message);
  }
  void append(int64_t first, int64_t count, int64_t total) {
    check(first >= 0 && count > 0 && count <= total && first <= total - count,
          "segment frame range");
    check(count_ <= total - count, "segment frame total");
    ranges_.push_back({first, count, count_});
    count_ += count;
  }
  void open() {
    if (segmented_) {
      const auto& range = ranges_[current_];
      stem_ = "segment-" + std::to_string(range.first) + "-" + std::to_string(range.count);
      activeOutput_ = directory_ / (stem_ + (lossless_ ? ".mkv" : ".mp4"));
      activeTemporary_ = activeOutput_;
      activeTemporary_ += ".partial";
    } else {
      activeOutput_ = output_;
      activeTemporary_ = temporary_;
    }
    check(!std::filesystem::exists(activeOutput_) && !std::filesystem::exists(activeTemporary_),
          "segment output exists");
    try {
      writer_ = std::make_unique<VideoWriter>(activeTemporary_, audio_, width_, height_,
                                             includeAudio_, true, fps_, den_, lossless_);
    } catch (...) {
      std::error_code ignored;
      std::filesystem::remove(activeTemporary_, ignored);
      throw;
    }
  }
  void close() {
    writer_->finish();
    writer_.reset();
    if (!segmented_) return;
    avatar::checkCancelled();
    std::filesystem::rename(activeTemporary_, activeOutput_);
    activeTemporary_.clear();
    if (segmented_) {
      const auto& range = ranges_[current_];
      J record = {{"path", std::filesystem::absolute(activeOutput_).string()},
                  {"firstFrame", range.first}, {"frameCount", range.count}};
      auto recordPath = directory_ / (stem_ + ".json");
      recordTemporary_ = recordPath.string() + ".partial";
      avatar::writeJson(recordPath, record);
      recordTemporary_.clear();
      records_.push_back(std::move(record));
    }
  }
  Path output_, temporary_, directory_, activeOutput_, activeTemporary_, recordTemporary_;
  const std::vector<float>& audio_;
  int width_, height_, fps_, den_;
  bool lossless_, segmented_, includeAudio_;
  int64_t count_ = 0, written_ = 0;
  size_t current_ = 0;
  std::string stem_;
  std::vector<Range> ranges_;
  std::unique_ptr<VideoWriter> writer_;
  J records_ = J::array();
};
} // namespace ditto
