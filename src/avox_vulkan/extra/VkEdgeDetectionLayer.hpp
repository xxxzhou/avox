#pragma once

#include <memory>

#include "../VkTemplate.hpp"
#include "../layer/VkLayer.hpp"

namespace avox {

class VkPrewittEdgeDetectionLayer : public VkLayer, public IParamet<float> {
  AVOX_LAYER_GETNAME(VkPrewittEdgeDetectionLayer)
  AVOX_VULKAN_PARAMETUPDATE()
 private:
  /* data */
 public:
  VkPrewittEdgeDetectionLayer(/* args */);
  virtual ~VkPrewittEdgeDetectionLayer();

 protected:
  virtual void onInitGraph() override;
};

class VkSobelEdgeDetectionLayer : public VkPrewittEdgeDetectionLayer {
  AVOX_LAYER_GETNAME(VkSobelEdgeDetectionLayer)
 private:
  /* data */
 public:
  VkSobelEdgeDetectionLayer(/* args */);
  virtual ~VkSobelEdgeDetectionLayer();
};

class VkSketchLayer : public VkPrewittEdgeDetectionLayer {
  AVOX_LAYER_GETNAME(VkSketchLayer)
 private:
  /* data */
 public:
  VkSketchLayer(/* args */);
  virtual ~VkSketchLayer();
};

class VkThresholdSketchLayer : public VkLayer,
                               public IParamet<ThresholdSobelParamet> {
  AVOX_LAYER_GETNAME(VkThresholdSketchLayer)
  AVOX_VULKAN_PARAMETUPDATE()
 protected:
  /* data */
  bool bSignal = true;

 public:
  VkThresholdSketchLayer(bool signalChannal = true);
  virtual ~VkThresholdSketchLayer();

 protected:
  virtual void onInitGraph() override;
};

// 按GPUImage2里来,实现同VkThresholdSketchLayer
class VkThresholdEdgeDetectionLayer : public VkThresholdSketchLayer {
  AVOX_LAYER_GETNAME(VkThresholdEdgeDetectionLayer)
  AVOX_VULKAN_PARAMETUPDATE()
 private:
  /* data */
 public:
  VkThresholdEdgeDetectionLayer(bool signalChannal = true);
  virtual ~VkThresholdEdgeDetectionLayer();
};

}