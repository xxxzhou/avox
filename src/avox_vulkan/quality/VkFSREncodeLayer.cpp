#include "VkFSREncodeLayer.hpp"

namespace avox {

VkFSREncodeLayer::VkFSREncodeLayer() {
  glslPath = "glsl/fsr_srgb_encode.comp.spv";
}

VkFSREncodeLayer::~VkFSREncodeLayer() {}

void VkFSREncodeLayer::onInitGraph() {
  inFormats[0].imageType = ImageType::rgba16f;
  outFormats[0].imageType = ImageType::rgba8;
  VkLayer::onInitGraph();
}

}
