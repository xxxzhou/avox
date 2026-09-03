#pragma once

#include "VkLayer.hpp"

namespace avox {


struct VkBlendParamet {
  // 0
  float fx;
  float fy;
  // 0
  float centerX;
  float centerY;
  float width;
  float height;
  // 不透明
  float opacity;
};

class VkBlendLayer : public VkLayer, public IBlendLayer {
  AVOX_LAYER_GETNAME(VkBlendLayer)
 private:
  VkBlendParamet vkParamet = {};

 public:
  VkBlendLayer(/* args */);
  virtual ~VkBlendLayer();

 private:
  void parametTransform();

 protected:
  virtual void onUpdateParamet() override;
  virtual bool getSampled(int inIndex) override;
};


}