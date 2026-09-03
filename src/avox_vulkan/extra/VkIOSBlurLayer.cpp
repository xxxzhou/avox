#include "VkIOSBlurLayer.hpp"

#include "../layer/VkPipeGraph.hpp"

namespace avox {

VkIOSBlurLayer::VkIOSBlurLayer(/* args */) { onUpdateParamet(); }

VkIOSBlurLayer::~VkIOSBlurLayer() {}

void VkIOSBlurLayer::onUpdateParamet() {
  downLayer->get()->updateParamet(
      {1, 1.0f / paramet.sacle, 1.0f / paramet.sacle});
  saturationLayer->get()->updateParamet(paramet.saturation);
  blurLayer->get()->updateParamet(paramet.blurParamet);
  lumRangeLayer->get()->updateParamet(paramet.range);
  upLayer->get()->updateParamet({1, paramet.sacle, paramet.sacle});
}

void VkIOSBlurLayer::onInitNode() {
  downLayer = vkPipeGraph->addNode<VkSizeScaleLayer>();
  saturationLayer = vkPipeGraph->addNode<VkSaturationLayer>();
  blurLayer = vkPipeGraph->addNode<VkGaussianBlurSLayer>();
  lumRangeLayer = vkPipeGraph->addNode<VkLuminanceRangeLayer>();
  upLayer = vkPipeGraph->addNode<VkSizeScaleLayer>();

  downLayer->addLine(downLayer)->addLine(saturationLayer)
      ->addLine(blurLayer)->addLine(lumRangeLayer)->addLine(upLayer);
  setStartNode(downLayer);
  setEndNode(upLayer);
}

}