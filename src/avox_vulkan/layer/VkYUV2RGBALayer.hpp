#pragma once

#include "VkLayer.hpp"
#include "avox/video/ColorSpace.hpp"

namespace avox {

class VkYUV2RGBALayer : public VkLayer, public IYUVLayer {
  AVOX_LAYER_GETNAME(VkYUV2RGBALayer)
 public:
  VkYUV2RGBALayer(/* args */);
  virtual ~VkYUV2RGBALayer();
  // 设置颜色空间(矩阵), 运行时重传 UBO, 不重建 graph
  void setColorSpace(const ColorSpaceDesc& c);
  // HDR 静态元数据(峰值亮度), 运行时重传 UBO, 不重建 graph
  void setHdrMeta(const HdrMeta& meta);
  // HDR 输出模式(forceHDR 跳过 tone map), 运行时重传 UBO, 不重建 graph
  void setHdrMode(HdrMode mode);

 protected:
  virtual void onUpdateParamet() override;
  virtual void onInitLayer() override;
  // 按 cs 刷新 colorMat 并重传整个 UBO(头字段保持)
  void refreshColorMat();

 protected:
  ColorSpaceDesc cs{YuvStandard::bt601, YuvRange::full};
  HdrMode hdrMode = HdrMode::follow;
  ColorYuvUBO uboData{};
};

}
