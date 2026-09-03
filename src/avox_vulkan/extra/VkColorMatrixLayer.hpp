#pragma once

#include <memory>

#include "../VkTemplate.hpp"
#include "../layer/VkLayer.hpp"

namespace avox {

class VkColorMatrixLayer : public VkLayer, public IParamet<ColorMatrixParamet> {
  AVOX_LAYER_GETNAME(VkColorMatrixLayer)
  AVOX_VULKAN_PARAMETUPDATE()
 private:
  /* data */
 public:
  VkColorMatrixLayer(/* args */);
  virtual ~VkColorMatrixLayer();
};

class VkHSBLayer : public VkLayer, public IHSBLayer {
  AVOX_LAYER_GETNAME(VkHSBLayer)
 private:
  /* data */
  ColorMatrixParamet paramet = {};

 public:
  VkHSBLayer(/* args */);
  virtual ~VkHSBLayer();

 private:
  void parametTransform();

 public:
  virtual void reset() override;
  virtual void rotateHue(const float& h) override;
  virtual void adjustSaturation(const float& h) override;
  virtual void adjustBrightness(const float& h) override;
};

class VkSepiaLayer : public VkLayer, public IParamet<float> {
  AVOX_LAYER_GETNAME(VkSepiaLayer)
 private:
  /* data */
  ColorMatrixParamet mparamet = {};

 public:
  VkSepiaLayer(/* args */);
  virtual ~VkSepiaLayer();

 protected:
  virtual void onUpdateParamet() override;
};

}