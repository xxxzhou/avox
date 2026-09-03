#include "VkGuidedLayer.hpp"

#include "../layer/VkPipeGraph.hpp"

namespace avox {

VkToMatLayer::VkToMatLayer() {
  glslPath = "glsl/guidedFilter1.comp.spv";
  inCount = 1;
  outCount = 3;
}

VkToMatLayer::~VkToMatLayer() {}

void VkToMatLayer::onInitGraph() {
  inFormats[0].imageType = ImageType::rgba32f;
  outFormats[0].imageType = ImageType::rgba32f;
  outFormats[1].imageType = ImageType::rgba32f;
  outFormats[2].imageType = ImageType::rgba32f;
  VkLayer::onInitGraph();
}

VkGuidedSolveLayer::VkGuidedSolveLayer() {
  setUBOSize(sizeof(paramet), true);
  glslPath = "glsl/guidedFilter2.comp.spv";
  inCount = 4;
  outCount = 1;
  paramet = 0.000001f;
  updateUBO(&paramet);
}

VkGuidedSolveLayer::~VkGuidedSolveLayer() {}

void VkGuidedSolveLayer::onInitGraph() {
  inFormats[0].imageType = ImageType::rgba32f;
  inFormats[1].imageType = ImageType::rgba32f;
  inFormats[2].imageType = ImageType::rgba32f;
  inFormats[3].imageType = ImageType::rgba32f;
  outFormats[0].imageType = ImageType::rgba32f;
  VkLayer::onInitGraph();
}

VkGuidedLayer::VkGuidedLayer(/* args */) {
  // self
  glslPath = "glsl/guidedMatting.comp.spv";
  inCount = 2;
  outCount = 1;
}

void VkGuidedLayer::onUpdateParamet() {
  if (paramet.boxSize != oldParamet.boxSize) {
    box1Layer->get()->updateParamet({paramet.boxSize, paramet.boxSize});
    box2Layer->get()->updateParamet({paramet.boxSize, paramet.boxSize});
    box3Layer->get()->updateParamet({paramet.boxSize, paramet.boxSize});
    box4Layer->get()->updateParamet({paramet.boxSize, paramet.boxSize});
    box5Layer->get()->updateParamet({paramet.boxSize, paramet.boxSize});
  }
  if (paramet.eps != oldParamet.eps) {
    guidedSlayerLayer->get()->updateParamet(paramet.eps);
  }
}

void VkGuidedLayer::onInitGroup() {
  convertLayer = vkPipeGraph->addNode<VkConvertImageLayer>();
  resizeLayer = vkPipeGraph->addNode<VkResizeLayer>(ImageType::rgba32f);
  toMatLayer = vkPipeGraph->addNode<VkToMatLayer>();
  box1Layer = vkPipeGraph->addNode<VkBoxBlurSLayer>(ImageType::rgba32f);
  box2Layer = vkPipeGraph->addNode<VkBoxBlurSLayer>(ImageType::rgba32f);
  box3Layer = vkPipeGraph->addNode<VkBoxBlurSLayer>(ImageType::rgba32f);
  box4Layer = vkPipeGraph->addNode<VkBoxBlurSLayer>(ImageType::rgba32f);
  guidedSlayerLayer = vkPipeGraph->addNode<VkGuidedSolveLayer>();
  box5Layer = vkPipeGraph->addNode<VkBoxBlurSLayer>(ImageType::rgba32f);
  resize1Layer = vkPipeGraph->addNode<VkResizeLayer>(ImageType::rgba32f);
  int32_t cwidth = 1920;
  int32_t cheight = 1080;
#if __ANDROID__
  cwidth = 1280;
  cheight = 720;
#endif
  int32_t swidth = divUp(cwidth, zoom);
  int32_t sheight = divUp(cheight, zoom);
  resizeLayer->get()->updateParamet({false, swidth, sheight});
  box1Layer->get()->updateParamet({paramet.boxSize, paramet.boxSize});
  box2Layer->get()->updateParamet({paramet.boxSize, paramet.boxSize});
  box3Layer->get()->updateParamet({paramet.boxSize, paramet.boxSize});
  box4Layer->get()->updateParamet({paramet.boxSize, paramet.boxSize});
  box5Layer->get()->updateParamet({paramet.boxSize, paramet.boxSize});
  guidedSlayerLayer->get()->updateParamet(paramet.eps);
  resize1Layer->get()->updateParamet({true, cwidth, cheight});

  // 输入输出
  inFormats[0].imageType = ImageType::rgba32f;
  inFormats[1].imageType = ImageType::rgba32f;
  outFormats[0].imageType = ImageType::rgba8;
  convertLayer->addLine(resizeLayer)->addLine(toMatLayer);
}

void VkGuidedLayer::onInitNode() {
  resizeLayer->addLine(box1Layer, 0, 0);
  toMatLayer->addLine(box2Layer, 0, 0);
  toMatLayer->addLine(box3Layer, 1, 0);
  toMatLayer->addLine(box4Layer, 2, 0);
  box1Layer->addLine(guidedSlayerLayer, 0, 0);
  box2Layer->addLine(guidedSlayerLayer, 0, 1);
  box3Layer->addLine(guidedSlayerLayer, 0, 2);
  box4Layer->addLine(guidedSlayerLayer, 0, 3);
  guidedSlayerLayer->addLine(box5Layer);
  box5Layer->addLine(resize1Layer);
  convertLayer->addLine(getNode(), 0, 0);
  resize1Layer->addLine(getNode(), 0, 1);
  setStartNode(convertLayer);
}

void VkGuidedLayer::onInitLayer() {
  VkLayer::onInitLayer();
  ImageFormat format = {};
  vkPipeGraph->getInSlot({resizeLayer->getNodeIndex(), 0}, format);
  int32_t width = format.width;
  int32_t height = format.height;
  int32_t scaleWidth = divUp(width, zoom);
  int32_t scaleHeight = divUp(height, zoom);
  resizeLayer->get()->updateParamet({false, scaleWidth, scaleHeight});
  resize1Layer->get()->updateParamet({true, width, height});
}

VkGuidedLayer::~VkGuidedLayer() {}

}