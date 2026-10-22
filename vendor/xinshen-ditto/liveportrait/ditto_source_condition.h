#pragma once
#include <array>
#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <opencv2/core.hpp>
#include "mediapipe/tasks/c/vision/face_landmarker/face_landmarker.h"

namespace ditto::condition_detail {
inline void check(MpStatus status, char *error) {
  const std::string message = error ? error : "Ditto source eye detection failed";
  if (error) MpErrorFree(error);
  if (status != 0) throw std::runtime_error(message);
}
struct EyeDetector {
  MpFaceLandmarkerPtr detector = nullptr;
  MpImagePtr image = nullptr;
  FaceLandmarkerResult result{};
  ~EyeDetector() {
    MpFaceLandmarkerCloseResult(&result);
    if (image) MpImageFree(image);
    if (detector) {
      char *error = nullptr;
      MpFaceLandmarkerClose(detector, &error);
      if (error) MpErrorFree(error);
    }
  }
};
// Matches upstream EyeAttrUtilsByMP on normalized 3D landmarks. Ball values are
// interleaved [Lx,Rx,Ly,Ry,Lz,Rz], not concatenated left and right vectors.
inline std::array<float, 8> eyeCondition(const std::filesystem::path &model,
                                       const cv::Mat &rgbCrop,
                                       std::array<float,3> *mouthGeometry = nullptr) {
  EyeDetector state;
  const auto path = model.string();
  FaceLandmarkerOptions options{};
  options.base_options.model_asset_path = path.c_str();
#if defined(__APPLE__)
  options.base_options.host_system = HOST_SYSTEM_MAC;
#elif defined(_WIN32)
  options.base_options.host_system = HOST_SYSTEM_WINDOWS;
#else
  options.base_options.host_system = HOST_SYSTEM_LINUX;
#endif
  options.base_options.delegate = CPU;
  options.running_mode = RunningMode::IMAGE;
  options.num_faces = 1;
  char *error = nullptr;
  const auto created = MpFaceLandmarkerCreate(&options, &state.detector, &error);
  check(created, error);
  error = nullptr;
  const auto im = MpImageCreateFromUint8Data(kMpImageFormatSrgb, rgbCrop.cols,
      rgbCrop.rows, rgbCrop.data, int(rgbCrop.total() * 3), &state.image, &error);
  check(im, error);
  error = nullptr;
  const auto detected = MpFaceLandmarkerDetectImage(
      state.detector, state.image, nullptr, &state.result, &error);
  check(detected, error);
  if (state.result.face_landmarks_count != 1 ||
      state.result.face_landmarks[0].landmarks_count < 478)
    throw std::runtime_error("Ditto: 无法提取源人物眼睛参考条件");
  const auto &landmarks = state.result.face_landmarks[0];
  auto point = [&](int i) {
    const auto &p = landmarks.landmarks[i];
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
      throw std::runtime_error("Ditto: invalid source eye landmarks");
    return cv::Vec3f(p.x, p.y, p.z);
  };
  auto length = [](const cv::Vec3f &v) { return std::sqrt(v.dot(v)); };
  if (mouthGeometry) {
    auto xy = [&](int i) { const auto p=point(i); return cv::Vec2f(p[0],p[1]); };
    const auto axis=xy(291)-xy(61);
    const float width=std::sqrt(axis.dot(axis));
    const auto eyeAxis=xy(263)-xy(33);
    const float eyes=std::sqrt(eyeAxis.dot(eyeAxis));
    if (width<1e-6f || eyes<1e-6f) throw std::runtime_error("Ditto: degenerate mouth geometry");
    const cv::Vec2f normal(-axis[1]/width,axis[0]/width);
    auto vertical=[&](int a,int b) { return std::abs((xy(a)-xy(b)).dot(normal)); };
    *mouthGeometry={vertical(14,13)/width,(vertical(0,13)+vertical(14,17))/eyes,width/eyes};
  }
  std::array<float, 8> values{};
  const int ids[2][9] = {{33,133,145,159,144,160,153,158,468},
                         {263,362,374,386,373,387,380,385,473}};
  for (int side = 0; side < 2; ++side) {
    const auto *i = ids[side];
    auto outer = point(i[0]), inner = point(i[1]);
    const float width = length(outer - inner);
    if (width < 1e-8f) throw std::runtime_error("Ditto: degenerate eye width");
    values[side] = (length(point(i[2]) - point(i[3])) +
                   length(point(i[4]) - point(i[5])) +
                   length(point(i[6]) - point(i[7]))) / (width + 1e-8f);
    const auto delta = point(i[8]) - (outer + inner) * .5f;
    const float distance = length(delta);
    const auto move = (delta / (distance + 1e-8f) -
                      (outer - inner) / (width + 1e-8f)) * distance;
    for (int axis = 0; axis < 3; ++axis) values[2 + axis * 2 + side] = move[axis];
  }
  return values;
}
} // namespace ditto::condition_detail
