#pragma once

#include "../VkTemplate.hpp"
#include "../layer/VkLayer.hpp"

namespace avox {

// FSR EASU (Edge Adaptive Spatial Upsampling) layer
// 保边放大, 输入sampler, 输出rgba16f, dispatch在输出分辨率
class VkFSREASULayer : public VkLayer {
  AVOX_LAYER_GETNAME(VkFSREASULayer)

 private:
  int32_t scale = 2;  // 放大倍数 (2 or 4)

 public:
  VkFSREASULayer();
  virtual ~VkFSREASULayer();

  void setScale(int32_t s) { scale = s; }
  int32_t getScale() const { return scale; }

 protected:
  virtual bool getSampled(int32_t inIndex) override;
  virtual bool sampledNearest(int32_t inIndex) override;
  virtual void onInitGraph() override;
  virtual void onInitLayer() override;
};

}
