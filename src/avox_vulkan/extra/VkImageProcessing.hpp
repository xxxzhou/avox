#pragma once

#include <memory>

#include "../VkTemplate.hpp"
#include "../layer/VkLayer.hpp"

namespace avox {

class VkSharpenLayer : public VkLayer, public IParamet<SharpenParamet> {
  AVOX_LAYER_GETNAME(VkSharpenLayer)
  AVOX_VULKAN_PARAMETUPDATE()
 private:
  /* data */
 public:
  VkSharpenLayer(/* args */);
  ~VkSharpenLayer();
};

class VkColorLBPLayer : public VkLayer {
  AVOX_LAYER_GETNAME(VkColorLBPLayer)
 private:
  /* data */
 public:
  VkColorLBPLayer(/* args */);
  virtual ~VkColorLBPLayer();
};

}