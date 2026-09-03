#pragma once

#include "../VkTemplate.hpp"
#include "../layer/VkLayer.hpp"

namespace avox {

// 线性光→sRGB 转换层 (FSR 线性空间出口)
// rgba16f(线性值) → rgba8(sRGB), 无 UBO, dispatch 在输入分辨率
class VkFSREncodeLayer : public VkLayer {
  AVOX_LAYER_GETNAME(VkFSREncodeLayer)

 public:
  VkFSREncodeLayer();
  virtual ~VkFSREncodeLayer();

 protected:
  virtual void onInitGraph() override;
};

}
