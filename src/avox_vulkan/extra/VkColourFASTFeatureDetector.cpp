#include "VkColourFASTFeatureDetector.hpp"

#include "../layer/VkPipeGraph.hpp"

namespace avox {

VkColourFASTFeatureDetector::VkColourFASTFeatureDetector(/* args */) {
  glslPath = "glsl/fastFeatureDetector.comp.spv";
  setUBOSize(sizeof(float));
  updateUBO(&paramet.offset);
  inCount = 2;
}

VkColourFASTFeatureDetector::~VkColourFASTFeatureDetector() {}

bool VkColourFASTFeatureDetector::getSampled(int inIndex) {
  return inIndex == 0;
}

void VkColourFASTFeatureDetector::onUpdateParamet() {
  if (paramet.offset != oldParamet.offset) {
    updateUBO(&paramet.offset);
    bParametChange = true;
  }
  if (paramet.boxSize != oldParamet.boxSize) {
    boxBlur->get()->updateParamet({paramet.boxSize, paramet.boxSize});
  }
}

void VkColourFASTFeatureDetector::onInitGroup() {
  boxBlur = vkPipeGraph->addNode<VkBoxBlurSLayer>(); 
  boxBlur->get()->updateParamet({paramet.boxSize, paramet.boxSize}); 
}
void VkColourFASTFeatureDetector::onInitNode() {
  boxBlur->addLine(getNode(), 0, 1);
  setStartNode(getNode(), 0, 0);
  setStartNode(boxBlur, 0, 0);
}

}
