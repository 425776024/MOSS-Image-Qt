#pragma once
#include <algorithm>
#include <cctype>
#include <limits>
#include <stdexcept>
#include <string>

namespace avatar {
// Keep small preparation/stitching nets on CPU by default. The old macOS
// choices must not pin Windows' expensive audio, diffusion and warp nets there.
inline bool windowsGpuDefault(const std::string &name, bool preferred) {
  return preferred || name == "hubert.onnx" || name == "warp.onnx" ||
         name == "denoise.onnx" || name == "denoise-condition.onnx" ||
         name == "denoise-step.onnx";
}
inline std::string modelDeviceVariable(std::string name) {
  const auto dot = name.rfind('.');
  if (dot != std::string::npos) name.resize(dot);
  for (char &c : name)
    c = std::isalnum(static_cast<unsigned char>(c))
        ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : '_';
  return "XINSHEN_ONNX_DEVICE_" + name;
}
inline std::string normalizedDevice(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
      [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (value.empty()) return "auto";
  if (value == "cpu" || value == "auto") return value;
  if (value == "cuda" || value == "gpu") return value;
  if (value == "dml" || value == "directml") return "directml";
  throw std::runtime_error("Invalid Windows ONNX device: " + value +
                           " (expected auto, gpu, cpu, cuda or directml)");
}
inline std::string windowsModelDevice(const std::string &name, bool preferred,
                                     const std::string &global, const std::string &local) {
  const auto all = normalizedDevice(global);
  if (all == "cpu") return "cpu";
  const auto model = normalizedDevice(local);
  if (model != "auto") return model;
  if (all != "auto") return all;
  return windowsGpuDefault(name, preferred) ? "gpu" : "cpu";
}
inline bool windowsGpuRequested(const std::string &name, bool preferred,
                                const std::string &global, const std::string &local) {
  return windowsModelDevice(name, preferred, global, local) != "cpu";
}
inline void requireAppleAcceleration(const std::string &global, bool appleSilicon) {
  if (!appleSilicon) throw std::runtime_error("Ditto on macOS requires Apple Silicon acceleration");
  std::string device = global;
  std::transform(device.begin(), device.end(), device.begin(), [](unsigned char c) { return std::tolower(c); });
  if (device == "cpu")
    throw std::runtime_error("CPU inference is disabled on macOS; use Apple Silicon acceleration");
  if (!device.empty() && device != "auto" && device != "gpu" && device != "metal" && device != "coreml")
    throw std::runtime_error("macOS inference requires Core ML / Metal on Apple Silicon");
}
inline int directMLDeviceIndex(const std::string &value, const char *variable = "XINSHEN_DML_DEVICE_ID") {
  if (value.empty()) return -1; // Select a hardware adapter automatically.
  int index = 0;
  for (unsigned char c : value) {
    if (c < '0' || c > '9' || index > (std::numeric_limits<int>::max() - (c - '0')) / 10)
      throw std::runtime_error(std::string(variable) + " must be a nonnegative adapter index");
    index = index * 10 + (c - '0');
  }
  return index;
}
} // namespace avatar
