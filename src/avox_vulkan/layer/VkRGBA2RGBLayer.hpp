#pragma once


#include "VkLayer.hpp"

namespace avox {


class VkRGBA2RGBLayer : public VkLayer {
  AVOX_LAYER_GETNAME(VkRGBA2RGBLayer)

 public:
  VkRGBA2RGBLayer(/* args */);
  virtual ~VkRGBA2RGBLayer();

 protected:
 virtual void onInitGraph() override;
  virtual void onInitLayer() override;
};


}