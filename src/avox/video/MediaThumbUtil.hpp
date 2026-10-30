#pragma once

// 抓帧纯逻辑: 输出尺寸与输入格式门。不依赖 FFmpeg/Vulkan, 供渲染器与单测共用。

#include <cmath>
#include <cstdint>

#include "avox/AvoxVideo.h"

namespace avox {

// 输出宽度钳位(reader 契约: 请求宽只是分配提示)
constexpr int32_t kThumbMinWidth = 48;
constexpr int32_t kThumbMaxWidth = 1280;

inline int32_t clampThumbWidth(int32_t width) {
  return width < kThumbMinWidth
             ? kThumbMinWidth
             : (width > kThumbMaxWidth ? kThumbMaxWidth : width);
}

// 显示方向高度: 请求宽 × 显示宽高比(90/270 交换编码宽高); 非法输入返回 0
inline int32_t thumbDisplayHeight(int32_t outWidth, int32_t srcWidth,
                                  int32_t srcHeight, int32_t rotation) {
  const bool bSwap = rotation == 90 || rotation == 270;
  const int32_t dispW = bSwap ? srcHeight : srcWidth;
  const int32_t dispH = bSwap ? srcWidth : srcHeight;
  if (outWidth <= 0 || dispW <= 0 || dispH <= 0) {
    return 0;
  }
  const int32_t height =
      static_cast<int32_t>(std::lround((double)outWidth * dispH / dispW));
  return height > 0 ? height : 1;
}

// VkYUV2RGBALayer 支持的输入格式; 444P/gray/12bit 一律不支持, 不做 CPU 兜底
inline bool thumbSupportsYuv(YuvType type) {
  switch (type) {
    case YuvType::yuv420P:
    case YuvType::yuv422P:
    case YuvType::nv12:
    case YuvType::yuv420P10:
    case YuvType::yuv422P10:
    case YuvType::p010:
      return true;
    default:
      return false;
  }
}

}  // namespace avox
