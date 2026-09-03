#pragma once

#include <memory>

#include "../VkTemplate.hpp"
#include "../layer/VkInputLayer.hpp"
#include "VkBlendingModeLayer.hpp"
#include "VkSeparableLinearLayer.hpp"

namespace avox {

class VkLookupLayer : public VkGroupLayer, public ILookupLayer {
  AVOX_LAYER_GETNAME(VkLookupLayer)
 private:
  /* data */
  VKTNodePtr<VkInputLayer> lookupLayer = nullptr;  
  std::unique_ptr<ImageBuffer> imageBuffer = nullptr;

 public:
  VkLookupLayer(/* args */);
  virtual ~VkLookupLayer();

 public:
  virtual void loadLookUp(uint8_t* data, int32_t size) override;
  virtual VInputLayer* getLookUpInputLayer() override;

 protected:
  virtual bool getSampled(int inIndex) override;
  virtual void onInitGroup() override;
  virtual void onInitNode() override;
};

class VkSoftEleganceLayer : public VkGroupLayer, public ISoftEleganceLayer {
 private:
  /* data */
  VKTNodePtr<VkLookupLayer> lookupLayer1 = nullptr;
  VKTNodePtr<VkLookupLayer> lookupLayer2 = nullptr;
  VKTNodePtr<VkAlphaBlendLayer> alphaBlendLayer = nullptr;
  VKTNodePtr<VkGaussianBlurSLayer> blurLayer = nullptr;

 public:
  VkSoftEleganceLayer(/* args */);
  virtual ~VkSoftEleganceLayer();

 public:
  virtual void loadLookUp1(uint8_t* data, int32_t size) override;
  virtual void loadLookUp2(uint8_t* data, int32_t size) override;

  virtual VInputLayer* getLookUpInputLayer1() override;
  virtual VInputLayer* getLookUpInputLayer2() override;

 protected:
  virtual void onUpdateParamet() override;
  virtual void onInitNode() override;
};

}