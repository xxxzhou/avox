#include "VkAdaptiveThresholdLayer.hpp"

#include "../layer/VkPipeGraph.hpp"

namespace avox {

VkAdaptiveThresholdLayer::VkAdaptiveThresholdLayer(/* args */) {
  setUBOSize(4);
  inCount = 2;
  outCount = 1;
  glslPath = "glsl/adaptiveThreshold.comp.spv"; 
}

VkAdaptiveThresholdLayer::~VkAdaptiveThresholdLayer() {}

void VkAdaptiveThresholdLayer::onUpdateParamet() {
  if (paramet.boxSize != oldParamet.boxSize) {
    boxBlur->get()->updateParamet({paramet.boxSize, paramet.boxSize}); 
  }
  if (paramet.offset != oldParamet.offset) {
    updateUBO(&paramet.offset);
    bParametChange = true;
  }
}

void VkAdaptiveThresholdLayer::onInitGroup() {  
  // 输入输出
  inFormats[0].imageType = ImageType::r8;
  inFormats[1].imageType = ImageType::r8;
  outFormats[0].imageType = ImageType::r8;
  // 这几个节点添加在本节点之前
  luminance = vkPipeGraph->addNode<VkLuminanceLayer>();
  boxBlur = vkPipeGraph->addNode<VkBoxBlurSLayer>(ImageType::r8);
  // kernel size会导致Graph重置,先更新下
  boxBlur->get()->updateParamet({paramet.boxSize, paramet.boxSize});
  vkPipeGraph->addLine(luminance,boxBlur);
}

void VkAdaptiveThresholdLayer::onInitNode() {
  luminance->addLine(getNode(), 0, 0);
  boxBlur->addLine(getNode(), 0, 1);
  setStartNode(luminance);
}

}