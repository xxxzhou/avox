#include "VkCropLayer.hpp"

namespace avox {

VkCropLayer::VkCropLayer(/* args */) {
  glslPath = "glsl/cropFilter.comp.spv";
  setUBOSize(sizeof(float) * 6);
  parametTransform();
}

VkCropLayer::~VkCropLayer() {}

bool VkCropLayer::parametTransform() {
  std::vector<float> paramets = {paramet.left,        paramet.top,
                                 paramet.fillColor.x, paramet.fillColor.y,
                                 paramet.fillColor.z, paramet.fillColor.w};
  updateUBO(paramets.data());
  bParametChange = true;
  if (paramet.width == oldParamet.width &&
      paramet.height == oldParamet.height) {
    return true;
  }
  return false;
}

void VkCropLayer::onUpdateParamet() {
  if (!parametTransform()) {
    resetGraph();
  }
}

void VkCropLayer::onInitLayer() {
  // 如果paramet里的width/height为0,则只取一个数据,可以给别层运算,移除map cpu
  parametTransform();
  if (paramet.width == 0 || paramet.height == 0) {
    outFormats[0].width = inFormats[0].width;
    outFormats[0].height = inFormats[0].height;
  } else {
    outFormats[0].width = paramet.width;
    outFormats[0].height = paramet.height;
  }

  sizeX = divUp(outFormats[0].width, groupX);
  sizeY = divUp(outFormats[0].height, groupY);
}

bool VkCropLayer::getSampled(int32_t inIndex) { return inIndex == 0; }

}