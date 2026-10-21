#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace videocut::ai::native {

enum class PlanarTrackingStatus : std::uint8_t {
  Tracked = 1,
  Predicted = 2,
  Lost = 3,
};

enum class PlanarTrackingPixelFormat : std::uint8_t {
  Gray8 = 1,
  Bgr8 = 2,
  Rgb8 = 3,
  Bgra8 = 4,
  Rgba8 = 5,
};

enum class PlanarTrackingDirection : std::uint8_t {
  Bidirectional = 1,
  Forward = 2,
  Backward = 3,
};

struct PlanarTrackingPoint final {
  double x{0.0};
  double y{0.0};
};

// Named order is part of the public contract. Geometry helpers interpret the
// perimeter as topLeft, topRight, bottomRight, bottomLeft.
struct PlanarTrackingQuad final {
  PlanarTrackingPoint topLeft;
  PlanarTrackingPoint topRight;
  PlanarTrackingPoint bottomLeft;
  PlanarTrackingPoint bottomRight;
};

// The producer is synchronous. The caller keeps [data] alive for the complete
// Initialize/Track call. Ticks are already resolved through the captured
// TimeWarp; the backend never invents or rounds source time.
struct PlanarTrackingFrameView final {
  std::int64_t clipLocalTicks{0};
  std::int64_t requestedSourceTicks{0};
  std::int64_t actualSourceTicks{0};
  std::uint32_t width{0};
  std::uint32_t height{0};
  std::size_t rowBytes{0};
  const std::uint8_t *data{nullptr};
  std::size_t dataBytes{0};
  PlanarTrackingPixelFormat pixelFormat{PlanarTrackingPixelFormat::Bgra8};
};

struct PlanarTrackerOptions final {
  std::uint32_t maximumFeatures{1400};
  std::uint32_t pyramidLevels{8};
  float pyramidScaleFactor{1.2F};
  std::uint32_t edgeThresholdPixels{31};
  std::uint32_t fastThreshold{20};
  double roiExpansionRatio{0.35};
  double knnRatio{0.75};
  double ransacReprojectionThresholdPixels{3.0};
  std::uint32_t ransacMaximumIterations{2000};
  double ransacConfidence{0.995};
  std::uint32_t minimumMatches{12};
  std::uint32_t minimumInliers{8};
  double minimumInlierRatio{0.45};
  double maximumMeanReprojectionErrorPixels{4.0};
  double maximumForwardBackwardErrorPixels{5.0};
  double minimumConfidence{0.35};
  double minimumNormalizedArea{0.00001};
  double maximumNormalizedArea{64.0};
  double maximumAreaScaleJump{4.0};
  double maximumNormalizedCoordinate{16.0};
  std::uint32_t maximumPredictionFrames{2};
  double predictionConfidenceDecay{0.55};
};

struct PlanarTrackingObservation final {
  std::int64_t clipLocalTicks{0};
  std::int64_t requestedSourceTicks{0};
  std::int64_t actualSourceTicks{0};
  std::int64_t referenceSourceTicks{0};
  PlanarTrackingStatus status{PlanarTrackingStatus::Lost};
  bool valid{false};
  std::optional<PlanarTrackingQuad> quad;
  // Reference-normalized to current-normalized, row-major. Invalid samples
  // retain identity here and expose no quad; consumers must honor [valid].
  std::array<double, 9> normalizedHomography{
      1.0, 0.0, 0.0,
      0.0, 1.0, 0.0,
      0.0, 0.0, 1.0};
  std::uint32_t matchCount{0};
  std::uint32_t inlierCount{0};
  double meanReprojectionErrorPixels{0.0};
  double confidence{0.0};
};

struct PlanarTrackingRequest final {
  std::vector<PlanarTrackingFrameView> frames;
  PlanarTrackingQuad initialQuad;
  // If absent, feature extraction uses the initial surface quad. This ROI is
  // only a detector constraint; observations always report [initialQuad].
  std::optional<PlanarTrackingQuad> initialRoi;
  // Clip-local identity disambiguates repeated source ticks introduced by a
  // freeze or reverse TimeWarp. Older callers may omit it and match the exact
  // actual/requested source tick instead.
  std::optional<std::int64_t> referenceClipLocalTicks;
  std::int64_t referenceSourceTicks{0};
  PlanarTrackingDirection direction{PlanarTrackingDirection::Bidirectional};
  PlanarTrackerOptions options;
};

struct PlanarTrackingResult final {
  // Always ordered by clip-local, requested-source, then actual-source ticks.
  std::vector<PlanarTrackingObservation> observations;
  bool complete{false};
  bool cancelled{false};
};

using PlanarTrackingCancellationHook = std::function<bool()>;
using PlanarTrackingProgressHook = std::function<void(
    std::size_t completedFrames,
    std::size_t totalFrames,
    const PlanarTrackingObservation &observation)>;

class PlanarTracker final {
public:
  [[nodiscard]] static std::unique_ptr<PlanarTracker>
  Create(const PlanarTrackerOptions &options, std::string *error = nullptr);

  ~PlanarTracker();
  PlanarTracker(PlanarTracker &&) noexcept;
  PlanarTracker &operator=(PlanarTracker &&) noexcept;
  PlanarTracker(const PlanarTracker &) = delete;
  PlanarTracker &operator=(const PlanarTracker &) = delete;

  // Initializes one directional session. Bidirectional production creates two
  // independent sessions from the same seed so state never leaks across it.
  [[nodiscard]] bool Initialize(
      const PlanarTrackingFrameView &referenceFrame,
      const PlanarTrackingQuad &initialQuad,
      std::int64_t referenceSourceTicks,
      const std::optional<PlanarTrackingQuad> &initialRoi = std::nullopt,
      std::string *error = nullptr);

  // Tracks exactly one adjacent frame in the session's traversal direction.
  // A geometric rejection is a successful call containing Predicted or Lost;
  // false is reserved for invalid input, cancellation, or an uninitialized
  // session.
  [[nodiscard]] bool Track(
      const PlanarTrackingFrameView &frame,
      PlanarTrackingObservation &observation,
      const PlanarTrackingCancellationHook &cancellation = {},
      std::string *error = nullptr);

  // Deterministic synchronous sequence producer. It starts forward/backward
  // sessions independently at the seed, polls cancellation before every
  // frame, and merges observations into clip-local tick order.
  [[nodiscard]] static bool Track(
      const PlanarTrackingRequest &request,
      PlanarTrackingResult &result,
      const PlanarTrackingCancellationHook &cancellation = {},
      const PlanarTrackingProgressHook &progress = {},
      std::string *error = nullptr);

private:
  struct Impl;
  explicit PlanarTracker(std::unique_ptr<Impl> impl) noexcept;

  std::unique_ptr<Impl> impl_;
};

} // namespace videocut::ai::native
