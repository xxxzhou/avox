#pragma once

#include <memory>

#include "../VkTemplate.hpp"
#include "VkSeparableLinearLayer.hpp"
#include "../layer/VkLayer.hpp"

namespace avox {

class VkXYDerivativeLayer : public VkLayer, public IParamet<float> {
  AVOX_LAYER_GETNAME(VkXYDerivativeLayer)
  AVOX_VULKAN_PARAMETUPDATE()
 public:
  VkXYDerivativeLayer();
  virtual ~VkXYDerivativeLayer();

 protected:
  virtual void onInitGraph() override;
};

// GPUImageThresholdedNonMaximumSuppressionFilter
class VkThresholdedNMS : public VkLayer, public IParamet<float> {
  AVOX_LAYER_GETNAME(VkThresholdedNMS)
  AVOX_VULKAN_PARAMETUPDATE()
 public:
  VkThresholdedNMS();
  virtual ~VkThresholdedNMS();

 protected:
  virtual void onInitGraph() override;
};

class VkHarrisDetectionBaseLayer : public VkGroupLayer {
 protected:
  /* data */
  VKTNodePtr<VkXYDerivativeLayer> xyDerivativeLayer;
  VKTNodePtr<VkGaussianBlurSLayer> blurLayer;
  VKTNodePtr<VkThresholdedNMS> thresholdNMSLayer;

 public:
  VkHarrisDetectionBaseLayer();
  virtual ~VkHarrisDetectionBaseLayer();

 protected:
  void baseParametChange(const HarrisDetectionBaseParamet& baseParamet);

 protected:
  virtual void onInitGroup() override;
  virtual void onInitNode() override;
};

class VkHarrisCornerDetectionLayer
    : public VkHarrisDetectionBaseLayer,
      public IParamet<HarrisCornerDetectionParamet> {
  AVOX_LAYER_GETNAME(VkHarrisCornerDetectionLayer)
 public:
  VkHarrisCornerDetectionLayer(/* args */);
  virtual ~VkHarrisCornerDetectionLayer();

 protected:
  void transformParamet();
  virtual void onUpdateParamet() override;
};

class VkNobleCornerDetectionLayer
    : public VkHarrisDetectionBaseLayer,
      public IParamet<NobleCornerDetectionParamet> {
  AVOX_LAYER_GETNAME(VkNobleCornerDetectionLayer)
 private:
  /* data */
 public:
  VkNobleCornerDetectionLayer(/* args */);
  virtual ~VkNobleCornerDetectionLayer();

 protected:
  void transformParamet();
  virtual void onUpdateParamet() override;
};

class VkShiTomasiFeatureDetectionLayer : public VkNobleCornerDetectionLayer {
  AVOX_LAYER_GETNAME(VkShiTomasiFeatureDetectionLayer)
 private:
  /* data */
 public:
  VkShiTomasiFeatureDetectionLayer(/* args */);
  virtual ~VkShiTomasiFeatureDetectionLayer();
};

}