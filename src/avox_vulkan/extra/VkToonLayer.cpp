#include "VkToonLayer.hpp"

#include "../layer/VkPipeGraph.hpp"
namespace avox {

VkToonLayer::VkToonLayer(/* args */) {
  glslPath = "glsl/toon.comp.spv";
  setUBOSize(sizeof(paramet), true);
  updateUBO(&paramet);
}

VkToonLayer::~VkToonLayer() {}

VkSmoothToonLayer::VkSmoothToonLayer(/* args */) {
  
}

VkSmoothToonLayer::~VkSmoothToonLayer() {}

void VkSmoothToonLayer::onUpdateParamet() {
  blurLayer->get()->updateParamet(paramet.blur);
  toonLayer->get()->updateParamet(paramet.toon);
}

void VkSmoothToonLayer::onInitNode() {
  blurLayer = vkPipeGraph->addNode<VkGaussianBlurSLayer>();
  toonLayer = vkPipeGraph->addNode<VkToonLayer>();
  blurLayer->addLine(toonLayer); 
  setStartNode(blurLayer);
  setEndNode(toonLayer);
  onUpdateParamet();
}

}
