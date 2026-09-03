#pragma once

#include <memory>

#include "../VkTemplate.hpp"
#include "../layer/VkLayer.hpp"

namespace avox {

// RGBA8->RGBA32F
class VkConvertImageLayer : public VkLayer {
  AVOX_LAYER_GETNAME(VkConvertImageLayer)
 private:
  /* data */
  ConvertType convert = ConvertType::rgba82rgba32f;

 public:
  explicit VkConvertImageLayer(ConvertType convert = ConvertType::rgba82rgba32f);
  virtual ~VkConvertImageLayer();

 protected:
  virtual void onInitGraph() override;
};

}
