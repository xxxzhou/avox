#pragma once

#include <memory>

#include "../VkTemplate.hpp"
#include "VkLuminanceLayer.hpp"
#include "../layer/VkLayer.hpp"

namespace avox {

class VkHistogramLayer : public VkLayer {
  AVOX_LAYER_GETNAME(VkHistogramLayer)
 private:
  /* data */
  int32_t channalCount = 1;

 public:
  VkHistogramLayer(bool signalChannal = true);
  virtual ~VkHistogramLayer();

 protected:
  virtual void onInitGraph() override;
  virtual void onInitLayer() override;
  virtual void onCommand() override;
};

class VkHistogramC4Layer : public VkGroupLayer {
  AVOX_LAYER_GETNAME(VkHistogramC4Layer)
 private:
  /* data */
  VKTNodePtr<VkHistogramLayer> preLayer = nullptr;

 public:
  VkHistogramC4Layer(/* args */);
  virtual ~VkHistogramC4Layer();

 protected:
  virtual void onInitGroup() override;
  virtual void onInitNode() override;
  virtual void onInitLayer() override;
};

class VkHistogramLutLayer : public VkLayer, public IParamet<int32_t> {
  AVOX_LAYER_GETNAME(VkHistogramLutLayer)
  AVOX_VULKAN_PARAMETUPDATE()
 public:
  VkHistogramLutLayer(/* args */);
  ~VkHistogramLutLayer();

 protected:
  virtual void onInitGraph() override;
  virtual void onInitLayer() override;
};

// 直方图均衡化
class VkEqualizeHistLayer : public VkGroupLayer {
  AVOX_LAYER_GETNAME(VkEqualizeHistLayer)
 private:
  /* data */
  VKTNodePtr<VkHistogramLayer> histLayer = nullptr;
  VKTNodePtr<VkHistogramLutLayer> lutLayer = nullptr;
  VKTNodePtr<VkLuminanceLayer> lumLayer = nullptr;
  bool bSignal = true;

 public:
  VkEqualizeHistLayer(bool signalChannal = true);
  ~VkEqualizeHistLayer();

 protected:
  virtual void onInitGroup() override;
  virtual void onInitNode() override;
  virtual void onInitLayer() override;
};

}