#pragma once

#include <memory>

#include "../VkTemplate.hpp"
#include "../layer/VkLayer.hpp"

namespace avox {

// 双边滤波
// 支持 rgba8 (sRGB 空间) 和 rgba16f (线性空间) 两种格式
class VkBilateralLayer : public VkLayer, public IParamet<BilateralParamet> {
  AVOX_LAYER_GETNAME(VkBilateralLayer)
 private:
  ImageType imageType = ImageType::rgba8;  // 默认 rgba8, FSR 管线用 rgba16f

 public:
  VkBilateralLayer(ImageType imageType = ImageType::rgba8);
  virtual ~VkBilateralLayer();

  void setImageType(ImageType t) { imageType = t; }

 private:
  void transformParamet();

 protected:
  virtual void onInitGraph() override;
  virtual void onUpdateParamet() override;
};

}
