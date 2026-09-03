#include "VkMorphLayer.hpp"

#include "../layer/VkPipeGraph.hpp"

namespace avox {

VkPreDilationLayer::VkPreDilationLayer(ImageType imageType_) {
  imageType = imageType_;
  glslPath = "glsl/morph1_dilation.comp.spv";
  if (imageType == ImageType::r8) {
    glslPath = "glsl/morph1_dilationC1.comp.spv";
  } else if (imageType == ImageType::r16) {
    glslPath = "glsl/morph1_dilationUI1.comp.spv";
  }
  setUBOSize(sizeof(paramet), true);
  paramet = 3;
  updateUBO(&paramet);
}

VkPreDilationLayer::~VkPreDilationLayer() {}

void VkPreDilationLayer::onInitGraph() {
  VkLayer::onInitGraph();
  inFormats[0].imageType = imageType;
  outFormats[0].imageType = imageType;
}

VkDilationLayer::VkDilationLayer(ImageType imageType_) {
  imageType = imageType_;
  glslPath = "glsl/morph2_dilation.comp.spv";
  if (imageType == ImageType::r8) {
    glslPath = "glsl/morph2_dilationC1.comp.spv";
  } else if (imageType == ImageType::r16) {
    glslPath = "glsl/morph2_dilationUI1.comp.spv";
  }
  setUBOSize(sizeof(paramet), true);
  paramet = 3;
  updateUBO(&paramet);
}

VkDilationLayer::~VkDilationLayer() {}

void VkDilationLayer::onUpdateParamet() {
  paramet = std::min(paramet, 32);
  if (paramet == oldParamet) {
    return;
  }
  preLayer->get()->updateParamet(paramet);
  updateUBO(&paramet);
  bParametChange = true;
}

void VkDilationLayer::onInitGroup() {  
  inFormats[0].imageType = imageType;
  outFormats[0].imageType = imageType;
  preLayer = vkPipeGraph->addNode<VkPreDilationLayer>();
}

void VkDilationLayer::onInitNode() {
  preLayer->addLine(getNode());
  setStartNode(preLayer);
}

VkPreErosionLayer::VkPreErosionLayer(ImageType imageType_) {
  imageType = imageType_;
  glslPath = "glsl/morph1_erosion.comp.spv";
  if (imageType == ImageType::r8) {
    glslPath = "glsl/morph1_erosionC1.comp.spv";
  } else if (imageType == ImageType::r16) {
    glslPath = "glsl/morph1_erosionUI1.comp.spv";
  }
  setUBOSize(sizeof(paramet), true);
  paramet = 3;
  updateUBO(&paramet);
}

VkPreErosionLayer::~VkPreErosionLayer() {}

void VkPreErosionLayer::onInitGraph() {
  VkLayer::onInitGraph();
  inFormats[0].imageType = imageType;
  outFormats[0].imageType = imageType;
}

VkErosionLayer::VkErosionLayer(ImageType imageType_) {
  imageType = imageType_;
  glslPath = "glsl/morph2_erosion.comp.spv";
  if (imageType == ImageType::r8) {
    glslPath = "glsl/morph2_erosionC1.comp.spv";
  } else if (imageType == ImageType::r16) {
    glslPath = "glsl/morph2_erosionUI1.comp.spv";
  }
  setUBOSize(sizeof(paramet), true);
  paramet = 3;
  updateUBO(&paramet);
}

VkErosionLayer::~VkErosionLayer() {}

void VkErosionLayer::onUpdateParamet() {
  paramet = std::min(paramet, 32);
  if (paramet == oldParamet) {
    return;
  }
  preLayer->get()->updateParamet(paramet);
  updateUBO(&paramet);
  bParametChange = true;
}

void VkErosionLayer::onInitGroup() {
  inFormats[0].imageType = imageType;
  outFormats[0].imageType = imageType;
  preLayer = vkPipeGraph->addNode<VkPreErosionLayer>();
}

void VkErosionLayer::onInitNode() {
  preLayer->addLine(getNode());
  setStartNode(preLayer);
}

VkClosingLayer::VkClosingLayer(ImageType imageType_) { imageType = imageType_; }

VkClosingLayer::~VkClosingLayer() {}

void VkClosingLayer::onUpdateParamet() {
  if (paramet == oldParamet) {
    return;
  }
  dilationLayer->get()->updateParamet(paramet);
  erosionLayer->get()->updateParamet(paramet);
}

void VkClosingLayer::onInitNode() {
  dilationLayer = vkPipeGraph->addNode<VkDilationLayer>();
  erosionLayer = vkPipeGraph->addNode<VkErosionLayer>();
  dilationLayer->addLine(erosionLayer);
  setStartNode(dilationLayer);
  setEndNode(erosionLayer);
}

VkOpeningLayer::VkOpeningLayer(ImageType imageType_) {
  imageType = imageType_;
}

VkOpeningLayer::~VkOpeningLayer() {}

void VkOpeningLayer::onUpdateParamet() {
  if (paramet == oldParamet) {
    return;
  }
  dilationLayer->get()->updateParamet(paramet);
  erosionLayer->get()->updateParamet(paramet);
}

void VkOpeningLayer::onInitNode() {
  dilationLayer = vkPipeGraph->addNode<VkDilationLayer>();
  erosionLayer = vkPipeGraph->addNode<VkErosionLayer>();
  erosionLayer->addLine(dilationLayer);
  setStartNode(erosionLayer);
  setEndNode(dilationLayer);
}

}