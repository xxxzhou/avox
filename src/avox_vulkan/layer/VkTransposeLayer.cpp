#include "VkTransposeLayer.hpp"

namespace avox {


VkTransposeLayer::VkTransposeLayer(/* args */) {
  glslPath = "glsl/transpose.comp.spv";
  setUBOSize(sizeof(TransposeParamet), true);
  updateUBO(&paramet);
}

VkTransposeLayer::~VkTransposeLayer() {}

void VkTransposeLayer::onInitLayer() {
  outFormats[0].width = inFormats[0].height;
  outFormats[0].height = inFormats[0].width;
  sizeX = divUp(outFormats[0].width, groupX);
  sizeY = divUp(outFormats[0].height, groupY);
}


}