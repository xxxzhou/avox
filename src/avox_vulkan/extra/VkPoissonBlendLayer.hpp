#pragma once

#include "../VkTemplate.hpp"
#include "VkLowPassLayer.hpp"
#include "../layer/VkLayer.hpp"

namespace avox {

class VkCopyImageLayer : public VkLayer {
  AVOX_LAYER_GETNAME(VkCopyImageLayer)
 private:
  /* data */
 public:
  VkCopyImageLayer(/* args */);
  virtual ~VkCopyImageLayer();
};

class VkPoissonBlendLayer : public VkGroupLayer, public IParamet<PoissonParamet> {
  AVOX_LAYER_GETNAME(VkPoissonBlendLayer)
 private:
  // 当前层会改变第一个输入的数据,所以内置一个层用来保存第一个输入
  VKTNodePtr<VkCopyImageLayer> copyLayer = nullptr;

 public:
  VkPoissonBlendLayer(/* args */);
  virtual ~VkPoissonBlendLayer();

 protected:
  virtual void onUpdateParamet() override;
  virtual void onInitGroup() override;
  virtual void onInitNode() override;
  virtual void onCommand() override;
};

}