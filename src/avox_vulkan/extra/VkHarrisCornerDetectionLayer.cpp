#include "VkHarrisCornerDetectionLayer.hpp"

#include "../layer/VkPipeGraph.hpp"

namespace avox {

VkXYDerivativeLayer::VkXYDerivativeLayer() {
  glslPath = "glsl/xyDerivative.comp.spv";
  setUBOSize(sizeof(float), true);
  paramet = 1.0f;
  updateUBO(&paramet);
}

VkXYDerivativeLayer::~VkXYDerivativeLayer() {}

void VkXYDerivativeLayer::onInitGraph() {
  VkLayer::onInitGraph();
  inFormats[0].imageType = ImageType::r8;
  outFormats[0].imageType = ImageType::rgba32f;
}

VkThresholdedNMS::VkThresholdedNMS() {
  glslPath = "glsl/thresholdedNMS.comp.spv";
  setUBOSize(sizeof(float), true);
  paramet = 0.9f;
  updateUBO(&paramet);
}

VkThresholdedNMS::~VkThresholdedNMS() {}

void VkThresholdedNMS::onInitGraph() {
  VkLayer::onInitGraph();
  inFormats[0].imageType = ImageType::r32f;
  outFormats[0].imageType = ImageType::r8;
}

VkHarrisDetectionBaseLayer::VkHarrisDetectionBaseLayer() {
}

VkHarrisDetectionBaseLayer::~VkHarrisDetectionBaseLayer() {}

void VkHarrisDetectionBaseLayer::baseParametChange(
    const HarrisDetectionBaseParamet& baseParamet) {
  xyDerivativeLayer->get()->updateParamet(baseParamet.edgeStrength);
  blurLayer->get()->updateParamet(baseParamet.blueParamet);
  thresholdNMSLayer->get()->updateParamet(baseParamet.threshold);
}

void VkHarrisDetectionBaseLayer::onInitGroup() {
  xyDerivativeLayer = vkPipeGraph->addNode<VkXYDerivativeLayer>();
  blurLayer = vkPipeGraph->addNode<VkGaussianBlurSLayer>(ImageType::rgba32f);
  thresholdNMSLayer = vkPipeGraph->addNode<VkThresholdedNMS>();
  xyDerivativeLayer->addLine(blurLayer);
  inFormats[0].imageType = ImageType::rgba32f;
  outFormats[0].imageType = ImageType::r32f;  
}

void VkHarrisDetectionBaseLayer::onInitNode() {
  blurLayer->addLine(getNode());
  getNode()->addLine(thresholdNMSLayer);
  setStartNode(xyDerivativeLayer);
  setEndNode(thresholdNMSLayer);
}

VkHarrisCornerDetectionLayer::VkHarrisCornerDetectionLayer(/* args */) {
  // self
  glslPath = "glsl/harrisCornerDetection.comp.spv";
  setUBOSize(8);
  baseParametChange(paramet.harrisBase);
  transformParamet();
}

VkHarrisCornerDetectionLayer::~VkHarrisCornerDetectionLayer() {}

void VkHarrisCornerDetectionLayer::transformParamet() {
  std::vector<float> paramets = {paramet.harris, paramet.sensitivity};
  updateUBO(paramets.data());
}

void VkHarrisCornerDetectionLayer::onUpdateParamet() {
  if (!(paramet.harrisBase == oldParamet.harrisBase)) {
    baseParametChange(paramet.harrisBase);
  }
  if (paramet.harris != oldParamet.harris ||
      paramet.sensitivity != oldParamet.sensitivity) {
    transformParamet();
    bParametChange = true;
  }
}

VkNobleCornerDetectionLayer::VkNobleCornerDetectionLayer(/* args */) {
  glslPath = "glsl/nobleCornerDetection.comp.spv";
  setUBOSize(4);
  paramet.sensitivity = 5.0f;
  baseParametChange(paramet.harrisBase);
  transformParamet();
}

VkNobleCornerDetectionLayer::~VkNobleCornerDetectionLayer() {}

void VkNobleCornerDetectionLayer::transformParamet() {
  updateUBO(&paramet.sensitivity);
}

void VkNobleCornerDetectionLayer::onUpdateParamet() {
  if (!(paramet.harrisBase == oldParamet.harrisBase)) {
    baseParametChange(paramet.harrisBase);
  }
  if (paramet.sensitivity != oldParamet.sensitivity) {
    transformParamet();
    bParametChange = true;
  }
}

VkShiTomasiFeatureDetectionLayer::VkShiTomasiFeatureDetectionLayer(/* args */) {
  glslPath = "glsl/shiTomasiFeatureDetection.comp.spv";
  paramet.sensitivity = 1.5f;
  transformParamet();
}

VkShiTomasiFeatureDetectionLayer::~VkShiTomasiFeatureDetectionLayer() {}

}