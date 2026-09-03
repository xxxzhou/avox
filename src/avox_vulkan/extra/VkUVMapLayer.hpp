#pragma once

#include <memory>

#include "../VkTemplate.hpp"
#include "../layer/VkInputLayer.hpp"
#include "../layer/VkLayer.hpp"

namespace avox {

class VkUVMapLayer : public VkGroupLayer, public IUVMapLayer {
  AVOX_LAYER_GETNAME(VkUVMapLayer)
 private:
  /* data */
  vec2i mapSize = {0, 0};
  VKTNodePtr<VkInputLayer> xMapLayer = nullptr;
  VKTNodePtr<VkInputLayer> yMapLayer = nullptr;

 public:
  VkUVMapLayer(bool bAdd);
  virtual ~VkUVMapLayer();

 public:
  virtual void updateMap(IImageBuffer* xMap, IImageBuffer* yMap) override;

 public:
  virtual bool getSampled(int inIndex) override;
  virtual void onInitGroup() override;
  virtual void onInitNode() override;
};

}