#pragma once
#ifdef _WIN32
#include "onnx_device_policy.h"
#include <windows.h>
#include <memory>

namespace avatar {
struct CudaAdapter { int index; std::string name; };
inline CudaAdapter cudaAdapter() {
  struct Library { HMODULE handle; ~Library() { if (handle) FreeLibrary(handle); } };
  Library driver{LoadLibraryExW(L"nvcuda.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)};
  if (!driver.handle) throw std::runtime_error("NVIDIA CUDA driver is unavailable");
  const auto init = reinterpret_cast<int(WINAPI *)(unsigned)>(GetProcAddress(driver.handle, "cuInit"));
  const auto countDevices = reinterpret_cast<int(WINAPI *)(int*)>(GetProcAddress(driver.handle, "cuDeviceGetCount"));
  const auto deviceName = reinterpret_cast<int(WINAPI *)(char*,int,int)>(GetProcAddress(driver.handle, "cuDeviceGetName"));
  const auto deviceMemory = reinterpret_cast<int(WINAPI *)(size_t*,int)>(GetProcAddress(driver.handle, "cuDeviceTotalMem_v2"));
  if (!init || !countDevices || !deviceName || !deviceMemory || init(0) != 0)
    throw std::runtime_error("NVIDIA CUDA driver initialization failed");
  int count = 0;
  if (countDevices(&count) != 0 || count < 1) throw std::runtime_error("No CUDA-capable NVIDIA GPU found");
  const char *configured = std::getenv("XINSHEN_CUDA_DEVICE_ID");
  const int requested = directMLDeviceIndex(configured ? configured : "", "XINSHEN_CUDA_DEVICE_ID");
  if (requested >= count) throw std::runtime_error("Requested CUDA adapter is unavailable");
  int selected = requested;
  if (selected < 0) {
    size_t largest = 0;
    for (int i = 0; i < count; ++i) {
      size_t memory = 0;
      if (deviceMemory(&memory, i) == 0 && (selected < 0 || memory > largest)) {
        selected = i; largest = memory;
      }
    }
  }
  if (selected < 0) throw std::runtime_error("Cannot inspect CUDA hardware memory");
  char name[256]{};
  if (deviceName(name, sizeof(name), selected) != 0) throw std::runtime_error("Cannot inspect CUDA adapter");
  return {selected, name};
}
} // namespace avatar
#endif
