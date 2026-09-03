#pragma once

#include <memory>

#include "../VkTemplate.hpp"
#include "../layer/VkLayer.hpp"
#include "../layer/VkResizeLayer.hpp"
#include "VkConvertImageLayer.hpp"
#include "VkLinearFilterLayer.hpp"
#include "VkSeparableLinearLayer.hpp"

namespace avox {

// 导向滤波求值 Guided filter
// 论文地址http://kaiminghe.com/publications/pami12guidedfilter.pdf
// https://www.cnblogs.com/zhouxin/p/10203954.html
class VkToMatLayer : public VkLayer {
  AVOX_LAYER_GETNAME(VkToMatLayer)
 public:
  VkToMatLayer();
  virtual ~VkToMatLayer();

 protected:
  virtual void onInitGraph() override;
};

class VkGuidedSolveLayer : public VkLayer, public IParamet<float> {
  AVOX_LAYER_GETNAME(VkGuidedSolveLayer)
  AVOX_VULKAN_PARAMETUPDATE()
 public:
  VkGuidedSolveLayer();
  virtual ~VkGuidedSolveLayer();

 protected:
  virtual void onInitGraph() override;
};

class VkGuidedLayer : public VkGroupLayer, public IParamet<GuidedParamet> {
  AVOX_LAYER_GETNAME(VkGuidedLayer)
 private:
  /* data */
  VKTNodePtr<VkConvertImageLayer> convertLayer = nullptr;
  VKTNodePtr<VkResizeLayer> resizeLayer = nullptr;
  VKTNodePtr<VkToMatLayer> toMatLayer = nullptr;
  VKTNodePtr<VkBoxBlurSLayer> box1Layer = nullptr;
  VKTNodePtr<VkBoxBlurSLayer> box2Layer = nullptr;
  VKTNodePtr<VkBoxBlurSLayer> box3Layer = nullptr;
  VKTNodePtr<VkBoxBlurSLayer> box4Layer = nullptr;
  VKTNodePtr<VkGuidedSolveLayer> guidedSlayerLayer = nullptr;
  VKTNodePtr<VkBoxBlurSLayer> box5Layer = nullptr;
  VKTNodePtr<VkResizeLayer> resize1Layer = nullptr;

  const int32_t zoom = 8;

 public:
  VkGuidedLayer(/* args */);
  virtual ~VkGuidedLayer();

 protected:
  virtual void onUpdateParamet() override;
  virtual void onInitGroup() override;
  virtual void onInitNode() override;
  virtual void onInitLayer() override;
};

}