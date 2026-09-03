#include "VkFSRRCASLayer.hpp"

#include "../layer/VkPipeGraph.hpp"

namespace avox {

VkFSRRCASLayer::VkFSRRCASLayer() {
  glslPath = "glsl/fsr_rcas.comp.spv";
  // UBO: 1 x float (sharpness)
  setUBOSize(sizeof(float));
}

VkFSRRCASLayer::~VkFSRRCASLayer() {}

void VkFSRRCASLayer::onInitGraph() {
  inFormats[0].imageType = ImageType::rgba16f;
  outFormats[0].imageType = ImageType::rgba16f;
  VkLayer::onInitGraph();
}

void VkFSRRCASLayer::onInitLayer() {
  // Same resolution, no scaling
  outFormats[0].width = inFormats[0].width;
  outFormats[0].height = inFormats[0].height;
  // Write sharpness to UBO
  updateUBO(&sharpness);
  sizeX = divUp(outFormats[0].width, groupX);
  sizeY = divUp(outFormats[0].height, groupY);
}

}
