#include "VkHistogramLayer.hpp"

#include "../layer/VkPipeGraph.hpp"

namespace avox {

VkHistogramLayer::VkHistogramLayer(bool signalChannal) {
  glslPath = "glsl/histogramC1.comp.spv";
  if (!signalChannal) {
    this->channalCount = 4;
    outCount = 4;
    glslPath = "glsl/histogram.comp.spv";
  }
  bMustClear = true;
}

VkHistogramLayer::~VkHistogramLayer() {}

void VkHistogramLayer::onInitGraph() {
  VkLayer::onInitGraph();
  inFormats[0].imageType = ImageType::r8;
  if (channalCount > 1) {
    inFormats[0].imageType = ImageType::rgba8;
  }
  for (int32_t i = 0; i < channalCount; i++) {
    outFormats[i].imageType = ImageType::r32;
  }
}

void VkHistogramLayer::onInitLayer() {
  VkLayer::onInitLayer();
  for (int32_t i = 0; i < channalCount; i++) {
    outFormats[i].width = 256;
    outFormats[i].height = 1;
  }
}

void VkHistogramLayer::onCommand() {
  clearColor({0, 0, 0, 0});
  VkLayer::onCommand();
}

VkHistogramC4Layer::VkHistogramC4Layer(/* args */) {
  glslPath = "glsl/histogramCombin.comp.spv";
  inCount = 4;
}

VkHistogramC4Layer::~VkHistogramC4Layer() {}

void VkHistogramC4Layer::onInitGroup() {  
  inFormats[0].imageType = ImageType::r32;
  inFormats[1].imageType = ImageType::r32;
  inFormats[2].imageType = ImageType::r32;
  inFormats[3].imageType = ImageType::r32;
  outFormats[0].imageType = ImageType::rgba32;
  preLayer = vkPipeGraph->addNode<VkHistogramLayer>(false);
}

void VkHistogramC4Layer::onInitNode() {
  preLayer->addLine(getNode(), 0, 0);
  preLayer->addLine(getNode(), 1, 1);
  preLayer->addLine(getNode(), 2, 2);
  preLayer->addLine(getNode(), 3, 3);
  setStartNode(preLayer);
}

void VkHistogramC4Layer::onInitLayer() {
  sizeX = divUp(inFormats[0].width, 256);
  sizeY = 1;
}

VkHistogramLutLayer::VkHistogramLutLayer() {
  glslPath = "glsl/histogramLut.comp.spv";
  setUBOSize(sizeof(paramet), true);
}

VkHistogramLutLayer::~VkHistogramLutLayer() {}

void VkHistogramLutLayer::onInitGraph() {
  VkLayer::onInitGraph();
  inFormats[0].imageType = ImageType::r32;
  outFormats[0].imageType = ImageType::r32f;
}

void VkHistogramLutLayer::onInitLayer() {
  sizeX = divUp(inFormats[0].width, 256);
  sizeY = 1;
}

VkEqualizeHistLayer::VkEqualizeHistLayer(bool signalChannal) {
  bSignal = signalChannal;
  glslPath = "glsl/histogramLutResultC1.comp.spv";
  if (!bSignal) {
    glslPath = "glsl/histogramLutResult.comp.spv";
  }
  inCount = 2;
}

VkEqualizeHistLayer::~VkEqualizeHistLayer() {}

void VkEqualizeHistLayer::onInitGroup() {  
  inFormats[0].imageType = bSignal ? ImageType::r8 : ImageType::rgba8;
  inFormats[1].imageType = ImageType::r32f;
  outFormats[0].imageType = bSignal ? ImageType::r8 : ImageType::rgba8;
  if (!bSignal) {
    lumLayer = vkPipeGraph->addNode<VkLuminanceLayer>();
  }
  histLayer = vkPipeGraph->addNode<VkHistogramLayer>(true);
  lutLayer = vkPipeGraph->addNode<VkHistogramLutLayer>();
  if (bSignal) {
    histLayer->addLine(lutLayer);
  } else {
    lumLayer->addLine(histLayer)->addLine(lutLayer);
  }
}

void VkEqualizeHistLayer::onInitNode() {
  lutLayer->addLine(getNode(), 0, 1);
  setStartNode(getNode());
  if (bSignal) {
    setStartNode(histLayer);
  } else {
    setStartNode(lumLayer);
  }
}

void VkEqualizeHistLayer::onInitLayer() {
  VkLayer::onInitLayer();
  ImageFormat iformt = {};
  vkPipeGraph->getInSlot({histLayer->getNodeIndex(), 0}, iformt);
  lutLayer->get()->updateParamet(iformt.width * iformt.height);
}

}