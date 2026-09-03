#pragma once

#include <memory>

#include "../VkTemplate.hpp"
#include "../layer/VkLayer.hpp"
#include "VkLuminanceLayer.hpp"
#include "VkSeparableLinearLayer.hpp"

namespace avox {

class VkDirectionalSobelEdgeDetectionLayer : public VkLayer,
                                             public IParamet<float> {
  AVOX_LAYER_GETNAME(VkDirectionalSobelEdgeDetectionLayer)
  AVOX_VULKAN_PARAMETUPDATE()
 public:
  VkDirectionalSobelEdgeDetectionLayer(/* args */);
  virtual ~VkDirectionalSobelEdgeDetectionLayer();

 protected:
  virtual void onInitGraph() override;
};

struct DirectionalNMSParamet {
  float minThreshold;
  float maxThreshold;

  inline bool operator==(const DirectionalNMSParamet& right) const {
    return this->minThreshold == right.minThreshold &&
           this->maxThreshold == right.maxThreshold;
  }
};

class VkDirectionalNMS : public VkLayer,
                         public IParamet<DirectionalNMSParamet> {
  AVOX_LAYER_GETNAME(VkDirectionalNMS)
  AVOX_VULKAN_PARAMETUPDATE()
 public:
  VkDirectionalNMS(/* args */);
  virtual ~VkDirectionalNMS();

 protected:
  virtual bool getSampled(int inIndex) override;
  virtual void onInitGraph() override;
};

class VkCannyEdgeDetectionLayer : public VkGroupLayer,
                                  public IParamet<CannyEdgeDetectionParamet> {
  AVOX_LAYER_GETNAME(VkCannyEdgeDetectionLayer)
 private:
  /* data */
  VKTNodePtr<VkLuminanceLayer> luminanceLayer = nullptr;
  VKTNodePtr<VkGaussianBlurSLayer> gaussianBlurLayer = nullptr;
  VKTNodePtr<VkDirectionalSobelEdgeDetectionLayer> sobelEDLayer = nullptr;
  VKTNodePtr<VkDirectionalNMS> directNMSLayer = nullptr;

 public:
  VkCannyEdgeDetectionLayer(/* args */);
  virtual ~VkCannyEdgeDetectionLayer();

 protected:
  virtual void onUpdateParamet() override;
  virtual void onInitGroup() override;
  virtual void onInitNode() override;
};

}