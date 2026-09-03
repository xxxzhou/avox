#pragma once

#include <memory>

#include "../VkTemplate.hpp"
#include "../layer/VkLayer.hpp"

namespace avox {

class VkPosterizeLayer : public VkLayer, public IParamet<uint32_t> {
  AVOX_LAYER_GETNAME(VkPosterizeLayer)
  AVOX_VULKAN_PARAMETUPDATE()
 private:
  /* data */
 public:
  VkPosterizeLayer(/* args */);
  virtual ~VkPosterizeLayer();
};

class VkVignetteLayer : public VkLayer, public IParamet<VignetteParamet> {
  AVOX_LAYER_GETNAME(VkVignetteLayer)
  AVOX_VULKAN_PARAMETUPDATE()
 private:
  /* data */
 public:
  VkVignetteLayer(/* args */);
  virtual ~VkVignetteLayer();
};

class VkCGAColorspaceLayer : public VkLayer {
  AVOX_LAYER_GETNAME(VkCGAColorspaceLayer)
 private:
  /* data */
 public:
  VkCGAColorspaceLayer(/* args */);
  virtual ~VkCGAColorspaceLayer();

 protected:
  virtual bool getSampled(int inIndex) override;
};

class VkCrosshatchLayer : public VkLayer, public IParamet<CrosshatchParamet> {
  AVOX_LAYER_GETNAME(VkCrosshatchLayer)
  AVOX_VULKAN_PARAMETUPDATE()
 private:
  /* data */
 public:
  VkCrosshatchLayer(/* args */);
  virtual ~VkCrosshatchLayer();
};

// The strength of the embossing, from  0.0 to 4.0, with 1.0 as the normal level
class VkEmbossLayer : public VkLayer, public IParamet<float> {
  AVOX_LAYER_GETNAME(VkEmbossLayer)
  AVOX_VULKAN_PARAMETUPDATE()
 private:
  /* data */
 public:
  VkEmbossLayer(/* args */);
  virtual ~VkEmbossLayer();
};

// 半径 1-32,默认为5
class VkKuwaharaLayer : public VkLayer, public IParamet<uint32_t> {
  AVOX_LAYER_GETNAME(VkKuwaharaLayer)
  AVOX_VULKAN_PARAMETUPDATE()
 private:
  /* data */
 public:
  VkKuwaharaLayer(/* args */);
  virtual ~VkKuwaharaLayer();
};

}