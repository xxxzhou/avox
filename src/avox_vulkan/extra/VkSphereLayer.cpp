#include "VkSphereLayer.hpp"

namespace avox {

VkSphereRefractionLayer::VkSphereRefractionLayer(/* args */) {
  glslPath = "glsl/sphereRefraction.comp.spv";
  setUBOSize(sizeof(paramet), true);
  updateUBO(&paramet);
}

VkSphereRefractionLayer::~VkSphereRefractionLayer() {}

bool VkSphereRefractionLayer::getSampled(int32_t inIndex) {
  return inIndex == 0;
}

VkGlassSphereLayer::VkGlassSphereLayer(/* args */) {
  glslPath = "glsl/glassSphere.comp.spv";
  setUBOSize(sizeof(paramet), true);
  updateUBO(&paramet);
}

VkGlassSphereLayer::~VkGlassSphereLayer() {}

bool VkGlassSphereLayer::getSampled(int32_t inIndex) { return inIndex == 0; }

}