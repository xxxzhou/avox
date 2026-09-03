#pragma once

#include "../VkTemplate.hpp"
#include "../layer/VkLayer.hpp"
#include "VkSeparableLinearLayer.hpp"

namespace avox {

class VkToonLayer : public VkLayer, public IParamet<ToonParamet> {
  AVOX_LAYER_GETNAME(VkToonLayer)
  AVOX_VULKAN_PARAMETUPDATE()
 private:
  /* data */
 public:
  VkToonLayer(/* args */);
  virtual ~VkToonLayer();
};

class VkSmoothToonLayer : public VkGroupLayer,
                          public IParamet<SmoothToonParamet> {
 private:
  /* data */
  VKTNodePtr<VkGaussianBlurSLayer> blurLayer = nullptr;
  VKTNodePtr<VkToonLayer> toonLayer = nullptr;

 public:
  VkSmoothToonLayer(/* args */);
  virtual ~VkSmoothToonLayer();

 protected:
  virtual void onUpdateParamet() override;
  virtual void onInitNode() override;
};

}