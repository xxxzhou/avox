#pragma once

#include <memory>

#include "../VkTemplate.hpp"
#include "VkSeparableLinearLayer.hpp"
#include "../layer/VkLayer.hpp"

namespace avox {

class VkBulgeDistortionLayer : public VkLayer,
                               public IParamet<DistortionParamet> {
  AVOX_LAYER_GETNAME(VkBulgeDistortionLayer)
  AVOX_VULKAN_PARAMETUPDATE()
 private:
  /* data */
 public:
  VkBulgeDistortionLayer(/* args */);
  virtual ~VkBulgeDistortionLayer();

 protected:
  virtual bool getSampled(int32_t inIndex) override;
};

class VkPinchDistortionLayer : public VkLayer,
                               public IParamet<DistortionParamet> {
  AVOX_LAYER_GETNAME(VkPinchDistortionLayer)
  AVOX_VULKAN_PARAMETUPDATE()
 private:
  /* data */
 public:
  VkPinchDistortionLayer(/* args */);
  ~VkPinchDistortionLayer();

 protected:
  virtual bool getSampled(int32_t inIndex) override;
};

class VkPixellatePositionLayer : public VkLayer,
                                 public IParamet<SelectiveParamet> {
  AVOX_LAYER_GETNAME(VkPixellatePositionLayer)
  AVOX_VULKAN_PARAMETUPDATE()
 private:
  /* data */
 public:
  VkPixellatePositionLayer(/* args */);
  ~VkPixellatePositionLayer();

 protected:
  virtual bool getSampled(int32_t inIndex) override;
};

class VkPolarPixellateLayer : public VkLayer,
                              public IParamet<PolarPixellateParamet> {
  AVOX_LAYER_GETNAME(VkPolarPixellateLayer)
  AVOX_VULKAN_PARAMETUPDATE()
 private:
  /* data */
 public:
  VkPolarPixellateLayer(/* args */);
  ~VkPolarPixellateLayer();

 protected:
  virtual bool getSampled(int32_t inIndex) override;
};

class VkStrectchDistortionLayer : public VkLayer, public IParamet<vec2f> {
  AVOX_LAYER_GETNAME(VkStrectchDistortionLayer)
  AVOX_VULKAN_PARAMETUPDATE()
 private:
  /* data */
 public:
  VkStrectchDistortionLayer(/* args */);
  ~VkStrectchDistortionLayer();

 protected:
  virtual bool getSampled(int32_t inIndex) override;
};

class VkSwirlLayer : public VkLayer, public IParamet<SwirlParamet> {
  AVOX_LAYER_GETNAME(VkSwirlLayer)
  AVOX_VULKAN_PARAMETUPDATE()
 private:
  /* data */
 public:
  VkSwirlLayer(/* args */);
  ~VkSwirlLayer();

 protected:
  virtual bool getSampled(int32_t inIndex) override;
};

}