#include "VkCannyEdgeDetectionLayer.hpp"

#include "../layer/VkPipeGraph.hpp"

namespace avox {

VkDirectionalSobelEdgeDetectionLayer::VkDirectionalSobelEdgeDetectionLayer(
    /* args */) {
  glslPath = "glsl/directionalSobel.comp.spv";
  setUBOSize(sizeof(paramet), true);
  paramet = 1.0f;
  updateUBO(&paramet);
}

VkDirectionalSobelEdgeDetectionLayer::~VkDirectionalSobelEdgeDetectionLayer() {}

void VkDirectionalSobelEdgeDetectionLayer::onInitGraph() {
  VkLayer::onInitGraph();
  inFormats[0].imageType = ImageType::r8;
  outFormats[0].imageType = ImageType::rgba32f;
}

VkDirectionalNMS::VkDirectionalNMS(/* args */) {
  glslPath = "glsl/directionalNMS.comp.spv";
  setUBOSize(sizeof(paramet), true);
  paramet.minThreshold = 0.1f;
  paramet.maxThreshold = 0.4f;
  updateUBO(&paramet);
}

VkDirectionalNMS::~VkDirectionalNMS() {}

bool VkDirectionalNMS::getSampled(int inIndex) {
  if (inIndex == 0) {
    return true;
  }
  return false;
}

void VkDirectionalNMS::onInitGraph() {
  VkLayer::onInitGraph();
  inFormats[0].imageType = ImageType::rgba32f;
  outFormats[0].imageType = ImageType::r32f;
}

VkCannyEdgeDetectionLayer::VkCannyEdgeDetectionLayer(/* args */) {
  glslPath = "glsl/canny.comp.spv";

  gaussianBlurLayer->get()->updateParamet(paramet.blueParamet);
  directNMSLayer->get()->updateParamet(
      {paramet.minThreshold, paramet.maxThreshold});
}

VkCannyEdgeDetectionLayer::~VkCannyEdgeDetectionLayer() {}

void VkCannyEdgeDetectionLayer::onUpdateParamet() {
  if (!(paramet.blueParamet == oldParamet.blueParamet)) {
    gaussianBlurLayer->get()->updateParamet(paramet.blueParamet);
  }
  if (paramet.minThreshold != oldParamet.minThreshold ||
      paramet.maxThreshold != oldParamet.maxThreshold) {
    directNMSLayer->get()->updateParamet(
        {paramet.minThreshold, paramet.maxThreshold});
  }
}

void VkCannyEdgeDetectionLayer::onInitGroup() {
  inFormats[0].imageType = ImageType::r32f;
  outFormats[0].imageType = ImageType::r8;
  luminanceLayer = vkPipeGraph->addNode<VkLuminanceLayer>();
  gaussianBlurLayer = vkPipeGraph->addNode<VkGaussianBlurSLayer>(ImageType::r8);
  sobelEDLayer = vkPipeGraph->addNode<VkDirectionalSobelEdgeDetectionLayer>();
  directNMSLayer = vkPipeGraph->addNode<VkDirectionalNMS>();
  // 更新参数,参数会重启,所以在这先更新一次
  gaussianBlurLayer->get()->updateParamet(paramet.blueParamet);
  directNMSLayer->get()->updateParamet(
      {paramet.minThreshold, paramet.maxThreshold});

  luminanceLayer->addLine(gaussianBlurLayer)
      ->addLine(sobelEDLayer)
      ->addLine(directNMSLayer);
}
void VkCannyEdgeDetectionLayer::onInitNode() {
  directNMSLayer->addLine(getNode());
  setStartNode(luminanceLayer);
}

}