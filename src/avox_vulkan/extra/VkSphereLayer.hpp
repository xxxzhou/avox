#pragma once

#include <memory>

#include "../VkTemplate.hpp"
#include "../layer/VkLayer.hpp"

namespace avox {

class VkSphereRefractionLayer : public VkLayer,
                                public IParamet<SphereRefractionParamet> {
  AVOX_LAYER_GETNAME(VkSphereRefractionLayer)
  AVOX_VULKAN_PARAMETUPDATE()
 private:
  /* data */
 public:
  VkSphereRefractionLayer(/* args */);
  ~VkSphereRefractionLayer();

 protected:
  virtual bool getSampled(int32_t inIndex) override;
};

class VkGlassSphereLayer : public VkLayer,
                           public IParamet<SphereRefractionParamet> {
  AVOX_LAYER_GETNAME(VkGlassSphereLayer)
  AVOX_VULKAN_PARAMETUPDATE()
 private:
  /* data */
 public:
  VkGlassSphereLayer(/* args */);
  ~VkGlassSphereLayer();

 protected:
  virtual bool getSampled(int32_t inIndex) override;
};

}