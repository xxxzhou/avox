#pragma once
#include "VkLayer.hpp"
#include "avox/video/ColorSpace.hpp"

namespace avox {

class VkRGBA2YUVLayer : public VkLayer, public IYUVLayer {
  AVOX_LAYER_GETNAME(VkRGBA2YUVLayer)
 public:
  VkRGBA2YUVLayer(/* args */);
  virtual ~VkRGBA2YUVLayer();
  // 设置颜色空间(矩阵), 运行时重传 UBO, 不重建 graph
  void setColorSpace(const ColorSpaceDesc& c);

 protected:
  virtual void onUpdateParamet() override;
  virtual void onInitLayer() override;
  // 按 cs 刷新 colorMat 并重传整个 UBO(头字段保持)
  void refreshColorMat();

 protected:
  ColorSpaceDesc cs{YuvStandard::bt601, YuvRange::full};
  ColorYuvUBO uboData{};
};


}
