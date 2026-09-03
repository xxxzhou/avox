#pragma once

#include <memory>

#include "../VkTemplate.hpp"
#include "../layer/VkLayer.hpp"

namespace avox {

class VkCropLayer : public VkLayer, public IParamet<CropParamet> {
  AVOX_LAYER_GETNAME(VkCropLayer)
 private:
  /* data */
 public:
  VkCropLayer(/* args */);
  virtual ~VkCropLayer();

 private:
  bool parametTransform();

 protected:
  virtual void onUpdateParamet() override;
  virtual void onInitLayer() override;
  virtual bool getSampled(int32_t inIndex) override;
};

}