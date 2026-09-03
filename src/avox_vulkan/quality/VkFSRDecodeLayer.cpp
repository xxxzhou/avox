#include "VkFSRDecodeLayer.hpp"

namespace avox {

VkFSRDecodeLayer::VkFSRDecodeLayer() {
  glslPath = "glsl/fsr_srgb_decode.comp.spv";
}

VkFSRDecodeLayer::~VkFSRDecodeLayer() {}

void VkFSRDecodeLayer::onInitGraph() {
  inFormats[0].imageType = ImageType::rgba8;
  outFormats[0].imageType = ImageType::rgba16f;
  VkLayer::onInitGraph();
}

}
