#pragma once

#include <memory>

#include "../VkTemplate.hpp"
#include "../layer/VkLayer.hpp"
#include "VkSeparableLinearLayer.hpp"

namespace avox {

class VkBlurBlendBaseLayer : public VkGroupLayer {
 protected:
  /* data */
  VKTNodePtr<VkGaussianBlurSLayer> blurLayer = nullptr;

 public:
  VkBlurBlendBaseLayer(/* args */);
  virtual ~VkBlurBlendBaseLayer();

 protected:
  void baseParametChange(const GaussianBlurParamet& baseParamet);

 protected:
  virtual void onInitGroup() override;
  virtual void onInitNode() override;
};

class VkGaussianBlurPositionLayer : public VkBlurBlendBaseLayer,
                                    public IParamet<BlurPositionParamet> {
  AVOX_LAYER_GETNAME(VkGaussianBlurPositionLayer)
 public:
  VkGaussianBlurPositionLayer(/* args */);
  virtual ~VkGaussianBlurPositionLayer();

 protected:
  virtual void onUpdateParamet() override;
  virtual bool getSampled(int32_t inIndex) override;
};

class VkGaussianBlurSelectiveLayer : public VkBlurBlendBaseLayer,
                                     public IParamet<BlurSelectiveParamet> {
  AVOX_LAYER_GETNAME(VkGaussianBlurSelectiveLayer)
 public:
  VkGaussianBlurSelectiveLayer(/* args */);
  virtual ~VkGaussianBlurSelectiveLayer();

 protected:
  virtual void onUpdateParamet() override;
  virtual bool getSampled(int32_t inIndex) override;
};

class VkTiltShiftLayer : public VkBlurBlendBaseLayer,
                         public IParamet<TiltShiftParamet> {
  AVOX_LAYER_GETNAME(VkTiltShiftLayer)
 public:
  VkTiltShiftLayer(/* args */);
  virtual ~VkTiltShiftLayer();

 protected:
  void transformParamet();
  virtual void onUpdateParamet() override;
};

class VkUnsharpMaskLayer : public VkBlurBlendBaseLayer,
                           public IParamet<UnsharpMaskParamet> {
  AVOX_LAYER_GETNAME(VkUnsharpMaskLayer)
 private:
  /* data */
 public:
  VkUnsharpMaskLayer(/* args */);
  virtual ~VkUnsharpMaskLayer();

 protected:
  virtual void onUpdateParamet() override;
};

}