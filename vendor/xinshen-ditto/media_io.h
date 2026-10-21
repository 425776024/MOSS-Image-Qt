#pragma once

#include <cstdint>
#include <algorithm>
#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

struct Image {
  int width = 0;
  int height = 0;
  std::vector<uint8_t> bgr;
  // Empty means opaque. Stored separately to preserve the legacy BGR renderer.
  std::vector<uint8_t> alpha;
  int64_t ptsUs = 0;
  int64_t durationUs = 40000;
  // A decoded RGB video flattened onto white needs its backplate removed when
  // applying an external matte. Keep RGB untouched for the neural network.
  std::optional<std::array<uint8_t,3>> matteBackgroundBgr;
  uint8_t premultipliedChannel(size_t index, size_t channel) const {
    const int a=alpha.empty() ? 255 : alpha[index];
    const int color=bgr[index*3+channel];
    const int value=matteBackgroundBgr
      ? color*255-(*matteBackgroundBgr)[channel]*(255-a) : color*a;
    return (std::clamp(value,0,a*255)+127)/255;
  }
  uint8_t* pixel(int x, int y) { return bgr.data() + (y * width + x) * 3; }
  const uint8_t* pixel(int x, int y) const { return bgr.data() + (y * width + x) * 3; }
};

struct MediaInfo {
  int width = 0, height = 0;
  int fpsNum = 25, fpsDen = 1;
  int rotation = 0;
  double pixelAspect = 1;
  int64_t durationUs = 0;
  bool hasAlpha = false;
  bool still = false;
};

std::vector<float> decodeAudio16k(const std::filesystem::path& path);
void writeImagePng(const std::filesystem::path& path, const Image& image);
// A video luminance matte is sampled at the color frame's source PTS, before
// resizing. Preserve the all-white legacy placeholder and any source alpha.
void applyVideoAlpha(Image& image, const Image& matte);

class VideoReader {
 public:
  explicit VideoReader(const std::filesystem::path& path);
  ~VideoReader();
  VideoReader(const VideoReader&) = delete;
  VideoReader& operator=(const VideoReader&) = delete;
  bool next(Image& out);
  void rewind();
  // Bounded two-frame random access, returns the frame covering this source PTS.
  bool readAt(int64_t timeUs, Image& out);
  MediaInfo info() const;
  int64_t quantizeTimeUs(int64_t timeUs) const;
 private:
  struct State;
  std::unique_ptr<State> state_;
};

class VideoWriter {
 public:
  VideoWriter(const std::filesystem::path& path, const std::vector<float>& audio,
              int width = 720, int height = 1280, bool includeAudio = true,
              bool preserveGeometry = false, int fpsNum = 25, int fpsDen = 1,
              bool lossless = false, bool sourceTimestamps = false);
  ~VideoWriter();
  VideoWriter(const VideoWriter&) = delete;
  VideoWriter& operator=(const VideoWriter&) = delete;
  void write(const Image& image);
  void finish();
 private:
  struct State;
  std::unique_ptr<State> state_;
};
