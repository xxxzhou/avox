#pragma once

#include "../VkTemplate.hpp"
#include "../layer/VkLayer.hpp"

namespace avox {

class VkLuminanceLayer : public VkLayer {
  AVOX_LAYER_GETNAME(VkLuminanceLayer)

 public:
  VkLuminanceLayer(/* args */);
  virtual ~VkLuminanceLayer();

 protected:
  virtual void onInitGraph() override;
};

// 降低亮度范围的程度,从0.0到1.0. 默认值为0.6.
class VkLuminanceRangeLayer : public VkLayer, public IParamet<float> {
  AVOX_LAYER_GETNAME(VkLuminanceRangeLayer)
  AVOX_VULKAN_PARAMETUPDATE()

 public:
  VkLuminanceRangeLayer(/* args */);
  virtual ~VkLuminanceRangeLayer();
};

class VkLuminanceThresholdLayer : public VkLayer, public IParamet<float> {
  AVOX_LAYER_GETNAME(VkLuminanceThresholdLayer)
  AVOX_VULKAN_PARAMETUPDATE()

 public:
  VkLuminanceThresholdLayer(/* args */);
  virtual ~VkLuminanceThresholdLayer();

 protected:
  virtual void onInitGraph() override;
};

}
