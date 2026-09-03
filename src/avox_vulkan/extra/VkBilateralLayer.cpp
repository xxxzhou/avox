#include "VkBilateralLayer.hpp"

namespace avox {
VkBilateralLayer::VkBilateralLayer(ImageType imageType_) {
  this->imageType = imageType_;
  if (imageType == ImageType::rgba16f) {
    glslPath = "glsl/bilateralH.comp.spv";
  } else {
    glslPath = "glsl/bilateral.comp.spv";
  }
  setUBOSize(sizeof(paramet));
  transformParamet();
}

VkBilateralLayer::~VkBilateralLayer() {}

void VkBilateralLayer::transformParamet() {
  BilateralParamet tparamet = {};
  tparamet.kernelSize = paramet.kernelSize;
  tparamet.sigma_color = -0.5f / (paramet.sigma_color * paramet.sigma_color);
  tparamet.sigma_spatial =
      -0.5f / (paramet.sigma_spatial * paramet.sigma_spatial);
  updateUBO(&tparamet);
}

void VkBilateralLayer::onInitGraph() {
  inFormats[0].imageType = imageType;
  outFormats[0].imageType = imageType;
  VkLayer::onInitGraph();
}

void VkBilateralLayer::onUpdateParamet() {
  if (paramet == oldParamet) {
    return;
  }
  transformParamet();
  bParametChange = true;  // 通知 onPreFrame submitUBO, 参数变化不依赖 graph 重建即可更新 GPU
}

}
