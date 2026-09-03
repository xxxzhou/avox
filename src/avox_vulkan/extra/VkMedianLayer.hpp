#pragma once

#include <memory>

#include "../VkTemplate.hpp"
#include "../layer/VkLayer.hpp"

namespace avox {

class VkMedianLayer : public VkLayer, public IParamet<uint32_t> {
  AVOX_LAYER_GETNAME(VkMedianLayer)
  AVOX_VULKAN_PARAMETUPDATE()
 private:
  /* data */
  bool bSingle = false;

 public:
  VkMedianLayer(bool bSingle = false);
  ~VkMedianLayer();

 protected:
  virtual void onInitGraph() override;
};

class VkMedianK3Layer : public VkLayer {
  AVOX_LAYER_GETNAME(VkMedianK3Layer)
 private:
  /* data */
  bool bSingle = false;

 public:
  VkMedianK3Layer(bool bSingle = false);
  ~VkMedianK3Layer();

 protected:
  virtual void onInitGraph() override;
};

}