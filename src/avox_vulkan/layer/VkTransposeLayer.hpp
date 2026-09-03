#pragma once

#include "VkLayer.hpp"

namespace avox {

class VkTransposeLayer : public VkLayer, public ITransposeLayer {
  AVOX_LAYER_GETNAME(VkTransposeLayer)
  AVOX_VULKAN_PARAMETUPDATE()
 private:
  /* data */
 public:
  VkTransposeLayer(/* args */);
  virtual ~VkTransposeLayer();

 public:
  virtual void onInitLayer() override;
};


}