#pragma once

#include "../VkTemplate.hpp"
#include "../layer/VkLayer.hpp"

namespace avox {

class VkPerlinNoiseLayer : public VkLayer, public IPerlinNoiseLayer {
  AVOX_LAYER_GETNAME(VkPerlinNoiseLayer)
  AVOX_VULKAN_PARAMETUPDATE()
 private:
  /* data */
  int32_t width = 0;
  int32_t height = 0;

 public:
  VkPerlinNoiseLayer(/* args */);
  ~VkPerlinNoiseLayer();

 public:
  virtual void setImageSize(int32_t width, int32_t height) override;
  virtual void onInitGraph() override;
  virtual void onInitLayer() override;
};

}