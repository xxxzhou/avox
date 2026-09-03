#pragma once

#include "VkLayer.hpp"

namespace avox {


class VkMapChannelLayer : public VkLayer, public IMapChannelLayer {
  AVOX_LAYER_GETNAME(VkMapChannelLayer)
  AVOX_VULKAN_PARAMETUPDATE()
 public:
  VkMapChannelLayer();
  virtual ~VkMapChannelLayer();
};

class VkFlipLayer : public VkLayer, public IFlipLayer {
  AVOX_LAYER_GETNAME(VkFlipLayer)
  AVOX_VULKAN_PARAMETUPDATE()
 public:
  VkFlipLayer();
  virtual ~VkFlipLayer();
};

// class VkOperateLayer : public VkLayer, public ITexOperateLayer {
//     AVOX_LAYER_GETNAME(VkOperateLayer)
//     AVOX_VULKAN_PARAMETUPDATE()
//    private:
//     /* data */
//    public:
//     VkOperateLayer(/* args */);
//     ~VkOperateLayer();
// };


}