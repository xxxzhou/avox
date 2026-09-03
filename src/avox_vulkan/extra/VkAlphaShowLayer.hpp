#pragma once

#include <memory>

#include "../VkTemplate.hpp"
#include "../layer/VkLayer.hpp"

namespace avox {

class VkAlphaShowLayer : public VkLayer {
  AVOX_LAYER_GETNAME(VkAlphaShowLayer)

 public:
  VkAlphaShowLayer();
  virtual ~VkAlphaShowLayer();

 protected:
  virtual void onInitLayer() override;
};

class VkAlphaShow2Layer : public VkLayer {
  AVOX_LAYER_GETNAME(VkAlphaShow2Layer)
 public:
  VkAlphaShow2Layer();
  virtual ~VkAlphaShow2Layer();

 protected:
  virtual void onInitGraph() override;
};

class VkAlphaSeparateLayer : public VkLayer {
  AVOX_LAYER_GETNAME(VkAlphaSeparateLayer)
 public:
  VkAlphaSeparateLayer();
  virtual ~VkAlphaSeparateLayer();

 protected:
  virtual void onInitGraph() override;
};

class VkAlphaCombinLayer : public VkLayer {
  AVOX_LAYER_GETNAME(VkAlphaCombinLayer)
 public:
  VkAlphaCombinLayer();
  virtual ~VkAlphaCombinLayer();

 protected:
  virtual void onInitGraph() override;
};

// 原图与mask图大小不一致
class VkAlphaScaleCombinLayer : public VkAlphaCombinLayer {
  AVOX_LAYER_GETNAME(VkAlphaScaleCombinLayer)
 public:
  VkAlphaScaleCombinLayer();
  virtual ~VkAlphaScaleCombinLayer();

 protected:
  virtual bool getSampled(int inIndex) override;
};

class VkTwoShowLayer : public VkLayer, public IParamet<float> {
  AVOX_LAYER_GETNAME(VkTwoShowLayer)
  AVOX_VULKAN_PARAMETUPDATE()
 public:
  VkTwoShowLayer(bool bRow = false);
  virtual ~VkTwoShowLayer();
};

}