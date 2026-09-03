#pragma once

#include <memory>

#include "../VkTemplate.hpp"
#include "../layer/VkLayer.hpp"
#include "../layer/VkResizeLayer.hpp"
#include "VkColorAdjustmentLayer.hpp"
#include "VkLuminanceLayer.hpp"
#include "VkSeparableLinearLayer.hpp"

namespace avox {

class VkIOSBlurLayer : public VkGroupLayer,
                       public IParamet<IOSBlurParamet> {
 public:
  VkIOSBlurLayer(/* args */);
  virtual ~VkIOSBlurLayer();

 private:
  VKTNodePtr<VkSizeScaleLayer> downLayer = nullptr;
  VKTNodePtr<VkSaturationLayer> saturationLayer = nullptr;
  VKTNodePtr<VkGaussianBlurSLayer> blurLayer = nullptr;
  VKTNodePtr<VkLuminanceRangeLayer> lumRangeLayer = nullptr;
  VKTNodePtr<VkSizeScaleLayer> upLayer = nullptr;

 protected:
  virtual void onUpdateParamet() override;
  virtual void onInitNode() override;
};

}