#pragma once

#include "../VkTemplate.hpp"
#include "VkLinearFilterLayer.hpp"
#include "VkLuminanceLayer.hpp"
#include "VkSeparableLinearLayer.hpp"
#include "../layer/VkLayer.hpp"

namespace avox {

// 确定像素周围的局部亮度,如果像素低于该局部亮度,则将其变为黑色,如果高于该像素,则将其变为白色.
class VkAdaptiveThresholdLayer : public VkGroupLayer,
                                 public IParamet<AdaptiveThresholdParamet> {
  AVOX_LAYER_GETNAME(VkAdaptiveThresholdLayer)
 private:
  /* data */
  // std::unique_ptr<VkLuminanceLayer> luminance;
  // std::unique_ptr<VkBoxBlurSLayer> boxBlur;
  VKTNodePtr<VkLuminanceLayer> luminance = nullptr;
  VKTNodePtr<VkBoxBlurSLayer> boxBlur = nullptr;

 public:
  VkAdaptiveThresholdLayer(/* args */);
  virtual ~VkAdaptiveThresholdLayer();

 protected:
  virtual void onUpdateParamet() override;
  virtual void onInitGroup() override;
  virtual void onInitNode() override;
};

}