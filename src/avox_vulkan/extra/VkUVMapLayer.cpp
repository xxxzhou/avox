#include "VkUVMapLayer.hpp"

#include "../layer/VkPipeGraph.hpp"

namespace avox {

VkUVMapLayer::VkUVMapLayer(bool bAdd) {
  inCount = 3;
  outCount = 1;
  if (bAdd) {
    glslPath = "glsl/addUVMap.comp.spv";
  } else {
    glslPath = "glsl/uvMap.comp.spv";
  }
  setUBOSize(sizeof(vec2i));
}

VkUVMapLayer::~VkUVMapLayer() {}

void VkUVMapLayer::updateMap(IImageBuffer* xMap, IImageBuffer* yMap) {
  xMapLayer->get()->inputCpuData(xMap, true);
  yMapLayer->get()->inputCpuData(yMap, true);
  ImageFormat imageFormat = xMap->getImageFormat();
  mapSize.x = imageFormat.width;
  mapSize.y = imageFormat.height;
  updateUBO(&mapSize);
  bParametChange = true;
}

bool VkUVMapLayer::getSampled(int inIndex) { return true; }

void VkUVMapLayer::onInitGroup() {
  xMapLayer = vkPipeGraph->addNode<VkInputLayer>();
  yMapLayer = vkPipeGraph->addNode<VkInputLayer>();
  // 格式
  inFormats[0].imageType = ImageType::rgba8;
  inFormats[1].imageType = ImageType::r32f;
  inFormats[2].imageType = ImageType::r32f;
  outFormats[0].imageType = ImageType::rgba8;
}

void VkUVMapLayer::onInitNode() {
  xMapLayer->addLine(getNode(), 0, 1);
  yMapLayer->addLine(getNode(), 0, 2);
  setStartNode(getNode());
}

}