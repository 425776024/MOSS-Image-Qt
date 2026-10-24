#pragma once
#ifdef MOSS_ONNX_DIRECTML
#include "onnx_device_policy.h"
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <cstdlib>
#include <string>

namespace avatar {
struct DirectMLAdapter {
  int index;
  std::string name;
};
inline DirectMLAdapter directMLAdapter() {
  using Microsoft::WRL::ComPtr;
  ComPtr<IDXGIFactory1> factory;
  if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
    throw std::runtime_error("Cannot enumerate DirectML hardware adapters");
  const char *configured = std::getenv("XINSHEN_DML_DEVICE_ID");
  const int requested = directMLDeviceIndex(configured ? configured : "");
  auto usable = [](IDXGIAdapter1 *adapter, DXGI_ADAPTER_DESC1 &desc) {
    return SUCCEEDED(adapter->GetDesc1(&desc)) &&
        !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
        SUCCEEDED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0,
                                  __uuidof(ID3D12Device), nullptr));
  };
  auto result = [](UINT index, const DXGI_ADAPTER_DESC1 &desc) {
    const int size = WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, nullptr, 0, nullptr, nullptr);
    std::string name(size > 0 ? size : 1, '\0');
    if (size > 0) WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name.data(), size, nullptr, nullptr);
    name.pop_back();
    return DirectMLAdapter{static_cast<int>(index), std::move(name)};
  };
  if (requested >= 0) {
    ComPtr<IDXGIAdapter1> adapter;
    DXGI_ADAPTER_DESC1 desc{};
    if (FAILED(factory->EnumAdapters1(static_cast<UINT>(requested), &adapter)) || !usable(adapter.Get(), desc))
      throw std::runtime_error("Requested DirectML adapter is unavailable or does not support DirectX 12");
    return result(static_cast<UINT>(requested), desc);
  }
  // DML's index uses EnumAdapters order, NOT EnumAdapterByGpuPreference order.
  // Resolve the high-performance adapter's LUID back to its original index.
  ComPtr<IDXGIFactory6> modern;
  if (SUCCEEDED(factory.As(&modern))) {
    for (UINT rank = 0;; ++rank) {
      ComPtr<IDXGIAdapter1> preferred;
      if (FAILED(modern->EnumAdapterByGpuPreference(rank, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                   IID_PPV_ARGS(&preferred)))) break;
      DXGI_ADAPTER_DESC1 wanted{};
      if (!usable(preferred.Get(), wanted)) continue;
      for (UINT index = 0;; ++index) {
        ComPtr<IDXGIAdapter1> adapter;
        if (FAILED(factory->EnumAdapters1(index, &adapter))) break;
        DXGI_ADAPTER_DESC1 desc{};
        if (SUCCEEDED(adapter->GetDesc1(&desc)) &&
            desc.AdapterLuid.LowPart == wanted.AdapterLuid.LowPart &&
            desc.AdapterLuid.HighPart == wanted.AdapterLuid.HighPart) return result(index, desc);
      }
    }
  }
  // Older DXGI: prefer dedicated video memory, and exclude software adapters.
  DirectMLAdapter best{-1, {}};
  SIZE_T memory = 0;
  for (UINT index = 0;; ++index) {
    ComPtr<IDXGIAdapter1> adapter;
    if (FAILED(factory->EnumAdapters1(index, &adapter))) break;
    DXGI_ADAPTER_DESC1 desc{};
    if (usable(adapter.Get(), desc) && (best.index < 0 || desc.DedicatedVideoMemory > memory)) {
      best = result(index, desc);
      memory = desc.DedicatedVideoMemory;
    }
  }
  if (best.index < 0) throw std::runtime_error("No DirectX 12 hardware adapter available for DirectML");
  return best;
}
} // namespace avatar
#endif
