#pragma once

#include "../VkTemplate.hpp"
#include "../layer/VkLayer.hpp"

namespace avox {

// sRGB→线性光 转换层 (FSR 线性空间入口)
// rgba8(sRGB) → rgba16f(线性值), 无 UBO, dispatch 在输入分辨率
class VkFSRDecodeLayer : public VkLayer {
  AVOX_LAYER_GETNAME(VkFSRDecodeLayer)

 public:
  VkFSRDecodeLayer();
  virtual ~VkFSRDecodeLayer();

 protected:
  virtual void onInitGraph() override;
};

}
