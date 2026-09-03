#pragma once

#include "../VkTemplate.hpp"
#include "../layer/VkLayer.hpp"

namespace avox {

// FSR RCAS (Robust Contrast Adaptive Sharpening) layer
// 可选锐化, 输入输出同分辨率, rgba16f→rgba16f
class VkFSRRCASLayer : public VkLayer {
  AVOX_LAYER_GETNAME(VkFSRRCASLayer)

 private:
  float sharpness = 0.0f;  // 0=最大锐化, N>0越弱

 public:
  VkFSRRCASLayer();
  virtual ~VkFSRRCASLayer();

  void setSharpness(float s) { sharpness = s; }

 protected:
  virtual void onInitGraph() override;
  virtual void onInitLayer() override;
};

}
