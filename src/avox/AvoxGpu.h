#pragma once
// Backend-neutral GPU frame. The gpu module maps native handles
// (D3D11 / Metal / GLES textures, AHardwareBuffer, IOSurface) into the
// Vulkan pipeline without copies.
#include <cstdint>

namespace avox {

enum class GpuBackend { none, vulkan, d3d11, d3d12, metal, gles };

struct GpuFrame {
  GpuBackend backend = GpuBackend::none;
  void* handle = nullptr;   // backend-specific texture / buffer object
  void* context = nullptr;  // context that owns `handle`
  int width = 0;
  int height = 0;
  int64_t ptsUs = 0;
};

}  // namespace avox
