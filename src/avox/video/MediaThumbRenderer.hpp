#pragma once

// IMediaReader 抓帧渲染器: 一帧 YUV 经 VK 管线出 RGBA8 到用户可读缓冲。
// 懒创建 SurfaceRenderVk; 首帧先渲同规格黑帧, 让 enableImage 支路重建落位
// (重建帧里用户缓冲的引用仍是旧的, 下一帧起才指向新映射)。

#include <cstdint>
#include <memory>
#include <vector>

#include "ImageBuffer.hpp"
#include "SurfaceRenderVk.hpp"

namespace avox {

class AVOX_EXPORT MediaThumbRenderer {
 public:
  MediaThumbRenderer();
  ~MediaThumbRenderer();

 public:
  // 与 AvoxRuntime::isVulkanAvailable 同源(进程内缓存)
  static bool available();
  // VkYUV2RGBALayer 支持的输入格式(444P/gray/12bit 不在其中, 抓帧不兜底)
  static bool supportsYuv(YuvType type);
  // 目标尺寸 = 请求宽度 × 源显示宽高比(90/270 交换编码宽高); 源尺寸变化自动重配
  bool prepare(int32_t requestWidth, int32_t srcWidth, int32_t srcHeight,
               int32_t rotation, YuvType type, const ColorSpaceDesc& cs);
  void setHdrMeta(const HdrMeta& meta);
  void setDoviMeta(const DoviMeta& meta);
  // 出图; 返回时 image() 指向本帧 RGBA8(下一次 render/析构前有效)
  bool render(const YUVFrame& frame);
  IImageBuffer* image();

 private:
  bool ensureRenderer();
  bool buildDummy(const YUVFormat& format, std::vector<uint8_t>& data,
                  YUVFrame* out) const;

 private:
  std::unique_ptr<SurfaceRenderVk> renderer;
  std::unique_ptr<ImageBuffer> outBuffer;
  // 首帧重建用的黑帧存储(与真实帧同源尺寸/类型)
  std::vector<uint8_t> dummyData;
  ColorSpaceDesc colorSpace = {};
  int32_t requestWidth = 0;
  int32_t srcWidth = 0;
  int32_t srcHeight = 0;
  int32_t rotation = 0;
  bool bPrepared = false;
  bool bDummyDone = false;
};

}  // namespace avox
