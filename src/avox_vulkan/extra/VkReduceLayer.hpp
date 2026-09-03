#pragma once

#include <memory>

#include "../VkTemplate.hpp"
#include "../layer/VkLayer.hpp"
#include "VkLuminanceLayer.hpp"

namespace avox {

class VkPreReduceLayer : public VkLayer {
  AVOX_LAYER_GETNAME(VkPreReduceLayer)
 protected:
  ReduceOperate reduceType = ReduceOperate::sum;

 public:
  VkPreReduceLayer(ReduceOperate operate,
                   ImageType imageType = ImageType::rgba8);
  virtual ~VkPreReduceLayer();

 protected:
  ImageType imageType = ImageType::rgba8;

 protected:
  virtual void onInitGraph() override;
  virtual void onInitLayer() override;
};

class VkReduceLayer : public VkGroupLayer {
  AVOX_LAYER_GETNAME(VkReduceLayer)
 protected:
  /* data */
  VKTNodePtr<VkPreReduceLayer> preLayer;
  ReduceOperate reduceType = ReduceOperate::sum;
  ImageType imageType = ImageType::rgba8;
  ImageType rimageType = ImageType::rgba8;

 public:
  VkReduceLayer(ReduceOperate operate, ImageType imageType = ImageType::rgba8);
  virtual ~VkReduceLayer();

 protected:
  virtual void onInitGroup() override;
  virtual void onInitNode() override;
  virtual void onInitLayer() override;
};

class VkAverageLuminanceThresholdLayer : public VkGroupLayer,
                                         public IParamet<float> {
  AVOX_LAYER_GETNAME(VkAverageLuminanceThresholdLayer)
  AVOX_VULKAN_PARAMETUPDATE()
 private:
  VKTNodePtr<VkLuminanceLayer> luminanceLayer;
  VKTNodePtr<VkReduceLayer> reduceLayer;

 public:
  VkAverageLuminanceThresholdLayer();
  virtual ~VkAverageLuminanceThresholdLayer();

 protected:
  virtual void onInitGroup() override;
  virtual void onInitNode() override;
};

}