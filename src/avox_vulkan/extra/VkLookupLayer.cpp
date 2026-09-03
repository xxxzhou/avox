#include "VkLookupLayer.hpp"

#include "../layer/VkPipeGraph.hpp"

namespace avox {

VkLookupLayer::VkLookupLayer(/* args */) {
  glslPath = "glsl/lookup.comp.spv";
  inCount = 2;
  outCount = 1;
  imageBuffer = std::make_unique<ImageBuffer>();
}

VkLookupLayer::~VkLookupLayer() {}

void VkLookupLayer::loadLookUp(uint8_t *data, int32_t size) {
  ImageType vtype = ImageType::other;
  if (size == 512 * 512 * 4) {
    vtype = ImageType::rgba8;
  } else if (size == 512 * 512 * 3) {
    vtype = ImageType::rgb8;
  } else {
    LOGASSERT(vtype == ImageType::other, "look up table image error");
  }
  ImageFormat vformat = {};
  vformat.width = 512;
  vformat.height = 512;
  vformat.imageType = vtype;
  imageBuffer->setData(data, vformat, true);
  lookupLayer->get()->inputCpuData(imageBuffer.get(), false);
}

VInputLayer *VkLookupLayer::getLookUpInputLayer() { return lookupLayer->get(); }

bool VkLookupLayer::getSampled(int inIndex) {
  if (inIndex == 1) {
    return true;
  }
  return false;
}

void VkLookupLayer::onInitGroup() {
  lookupLayer = vkPipeGraph->addNode<VkInputLayer>();
}

void VkLookupLayer::onInitNode() {
  lookupLayer->addLine(getNode(), 0, 1);
  setStartNode(getNode());
}

VkSoftEleganceLayer::VkSoftEleganceLayer(/* args */) {}

VkSoftEleganceLayer::~VkSoftEleganceLayer() {}

void VkSoftEleganceLayer::loadLookUp1(uint8_t *data, int32_t size) {
  lookupLayer1->get()->loadLookUp(data, size);
}

void VkSoftEleganceLayer::loadLookUp2(uint8_t *data, int32_t size) {
  lookupLayer2->get()->loadLookUp(data, size);
}

VInputLayer *VkSoftEleganceLayer::getLookUpInputLayer1() {
  return lookupLayer1->get()->getLookUpInputLayer();
}

VInputLayer *VkSoftEleganceLayer::getLookUpInputLayer2() {
  return lookupLayer2->get()->getLookUpInputLayer();
}

void VkSoftEleganceLayer::onUpdateParamet() {
  blurLayer->get()->updateParamet(paramet.blur);
  alphaBlendLayer->get()->updateParamet(paramet.mix);
}

void VkSoftEleganceLayer::onInitNode() {
  lookupLayer1 = vkPipeGraph->addNode<VkLookupLayer>();
  lookupLayer2 = vkPipeGraph->addNode<VkLookupLayer>();
  blurLayer = vkPipeGraph->addNode<VkGaussianBlurSLayer>();
  alphaBlendLayer = vkPipeGraph->addNode<VkAlphaBlendLayer>();
  lookupLayer1->addLine(alphaBlendLayer)->addLine(lookupLayer2);
  lookupLayer1->addLine(blurLayer, 0, 1);
  setStartNode(lookupLayer1);
  setEndNode(lookupLayer2);
  onUpdateParamet();
}

}