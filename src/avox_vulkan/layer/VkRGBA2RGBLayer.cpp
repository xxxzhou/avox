#include "VkRGBA2RGBLayer.hpp"

#include "VkPipeGraph.hpp"

namespace avox {


VkRGBA2RGBLayer::VkRGBA2RGBLayer(/* args */) {
  glslPath = "glsl/rgba2rgb.comp.spv";
}

VkRGBA2RGBLayer::~VkRGBA2RGBLayer() {}

void VkRGBA2RGBLayer::onInitGraph() {
  inFormats[0].imageType = ImageType::rgba8;
  outFormats[0].imageType = ImageType::r8;
  VkLayer::onInitGraph();
}

void VkRGBA2RGBLayer::onInitLayer() {
  outFormats[0].width = inFormats[0].width * 3;
  outFormats[0].height = inFormats[0].height;
  VkLayer::onInitLayer();
}


}