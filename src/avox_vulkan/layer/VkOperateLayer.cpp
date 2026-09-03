#include "VkOperateLayer.hpp"

namespace avox {


VkMapChannelLayer::VkMapChannelLayer(/* args */) {
  glslPath = "glsl/mapChannel.comp.spv";
  setUBOSize(sizeof(paramet), true);
  updateUBO(&paramet);
}

VkMapChannelLayer::~VkMapChannelLayer() {}

VkFlipLayer::VkFlipLayer(/* args */) {
  glslPath = "glsl/flip.comp.spv";
  setUBOSize(sizeof(paramet), true);
  updateUBO(&paramet);
}

VkFlipLayer::~VkFlipLayer() {}

// VkOperateLayer::VkOperateLayer(/* args */) {
//     glslPath = "glsl/operate.comp.spv";
//     setUBOSize(sizeof(paramet), true);
//     updateUBO(&paramet);
// }

// VkOperateLayer::~VkOperateLayer() {}


}