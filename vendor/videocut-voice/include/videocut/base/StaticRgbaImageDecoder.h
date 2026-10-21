#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace videocut::base {

enum class StaticImageFormat : std::uint8_t {
  Ppm = 0,
  Png,
  Jpeg,
  Webp,
};

enum class StaticImageColorPolicy : std::uint8_t {
  ConvertToSrgb,
  PreserveEncodedChannels,
};

struct StaticRgbaImageDecodeLimits final {
  std::uint64_t maximumFileBytes{64ULL * 1024ULL * 1024ULL};
  std::uint32_t maximumWidth{4096U};
  std::uint32_t maximumHeight{4096U};
  std::uint64_t maximumPixelBytes{64ULL * 1024ULL * 1024ULL};
};

struct StaticImageFormatPolicy final {
  bool admitPng{true};
  bool admitJpeg{true};
  bool admitPpm{true};
  bool admitWebp{true};
};

struct StraightRgba8Frame final {
  std::uint32_t width{0U};
  std::uint32_t height{0U};
  std::vector<std::uint8_t> pixels;

  [[nodiscard]] bool valid() const noexcept;
};

struct StaticRgbaImageDecodeResult final {
  std::optional<StraightRgba8Frame> frame;
  std::optional<StaticImageFormat> format;
  std::string error;

  [[nodiscard]] bool ok() const noexcept {
    return frame.has_value() && frame->valid() && format.has_value();
  }
};

// Explicit static-image ingress sizing. Resamples straight sRGB RGBA8 using
// the platform's high-quality image filter, retaining top-left byte rows.
// This is separate from decode so callers can validate source dimensions
// before sizing. Unsupported platforms fail without publishing an output.
[[nodiscard]] bool ResizeStaticRgbaImage(
    const StraightRgba8Frame &source, std::uint32_t width,
    std::uint32_t height, StraightRgba8Frame &output, std::string &error,
    const StaticRgbaImageDecodeLimits &limits = {});

// Renderer-neutral, fail-closed ingress for bounded static image bytes.
// Content signature owns format selection. File decoding uses one no-follow
// regular-file read, and decoded output is compact, top-left-origin straight
// RGBA8. Multi-frame sources and authored rotations/mirrors (orientation
// values 2..8) are rejected; absent metadata, unspecified 0, and upright 1
// require no authored-orientation transform and remain admitted. The platform
// implementation fixes the CGBitmapContext byte-row contract to CGImage's
// top-to-bottom storage order and never applies a UIKit presentation flip.
// PreserveEncodedChannels admits only PNG with supported, unassociated 8-bit
// decoded storage, copying its channels without color or alpha conversion.
class StaticRgbaImageDecoder final {
public:
  StaticRgbaImageDecoder() = delete;

  [[nodiscard]] static StaticRgbaImageDecodeResult DecodeBytes(
      std::string_view bytes,
      const StaticRgbaImageDecodeLimits &limits = {},
      const StaticImageFormatPolicy &policy = {},
      StaticImageColorPolicy colorPolicy = StaticImageColorPolicy::ConvertToSrgb);

  // Unlike application-specific adapters, the neutral file boundary does not
  // consult the filename or suffix. The admitted content signature is the
  // only format authority.
  [[nodiscard]] static StaticRgbaImageDecodeResult DecodeFile(
      const std::filesystem::path &path,
      const StaticRgbaImageDecodeLimits &limits = {},
      const StaticImageFormatPolicy &policy = {},
      StaticImageColorPolicy colorPolicy = StaticImageColorPolicy::ConvertToSrgb);
};

} // namespace videocut::base
