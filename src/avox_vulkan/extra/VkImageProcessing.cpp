#include "VkImageProcessing.hpp"

namespace avox {

VkSharpenLayer::VkSharpenLayer(/* args */) {
  glslPath = "glsl/sharpen.comp.spv";
  setUBOSize(sizeof(paramet), true);
  updateUBO(&paramet);
}

VkSharpenLayer::~VkSharpenLayer() {}

VkColorLBPLayer::VkColorLBPLayer(/* args */) {
  glslPath = "glsl/colorLocalBinaryPattern.comp.spv";
}

VkColorLBPLayer::~VkColorLBPLayer() {}

}