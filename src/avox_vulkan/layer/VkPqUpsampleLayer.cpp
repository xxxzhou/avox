#include "VkPqUpsampleLayer.hpp"

#include "VkPipeGraph.hpp"

namespace avox {

VkPqUpsampleLayer::VkPqUpsampleLayer(/* args */) {
  glslPath = "glsl/pqUpsample.comp.spv";
}

VkPqUpsampleLayer::~VkPqUpsampleLayer() {}

void VkPqUpsampleLayer::onInitGraph() {
  inFormats[0].imageType = ImageType::rgba8;
  outFormats[0].imageType = ImageType::rgba16f;
  VkLayer::onInitGraph();
}

void VkPqUpsampleLayer::onInitLayer() {
  // 逐像素一一对应, 尺寸不变(只换格式与值域)
  outFormats[0].width = inFormats[0].width;
  outFormats[0].height = inFormats[0].height;
  sizeX = divUp(outFormats[0].width, groupX);
  sizeY = divUp(outFormats[0].height, groupY);
  VkLayer::onInitLayer();
}

}
