#include "VkReduceLayer.hpp"

#include "../layer/VkPipeGraph.hpp"

// 一个线程处理每行PATCH_SIZE_X个元素
const int PATCH_SIZE_X = 4;
// 一个线程处理每列PATCH_SIZE_Y个元素
const int PATCH_SIZE_Y = 4;

namespace avox {

ImageType reduceConvert(ImageType imageType) {
  ImageType resultType = imageType;
  if (imageType == ImageType::rgba8 || imageType == ImageType::rgba32f) {
    resultType = ImageType::rgba32f;
  } else if (imageType == ImageType::r8 || imageType == ImageType::r32f) {
    resultType = ImageType::r32f;
  }
  return resultType;
}

VkPreReduceLayer::VkPreReduceLayer(ReduceOperate operate,
                                   ImageType imageType_) {
  reduceType = operate;
  imageType = imageType_;
  glslPath = "glsl/reduce_sum.comp.spv";
  if (imageType == ImageType::r8) {
    glslPath = "glsl/reduce_sumC1.comp.spv";
  }
}

VkPreReduceLayer::~VkPreReduceLayer() {}

void VkPreReduceLayer::onInitGraph() {
  inFormats[0].imageType = imageType;
  outFormats[0].imageType = reduceConvert(imageType);

  VkLayer::onInitGraph();
}

void VkPreReduceLayer::onInitLayer() {
  int32_t ngroupX = PATCH_SIZE_X * groupX;
  int32_t ngroupY = PATCH_SIZE_Y * groupY;

  sizeX = divUp(inFormats[0].width, ngroupX);
  sizeY = divUp(inFormats[0].height, ngroupY);

  outFormats[0].width = sizeX * sizeY;
  outFormats[0].height = 1;
}

VkReduceLayer::VkReduceLayer(ReduceOperate operate, ImageType imageType_) {
  imageType = imageType_;
  rimageType = reduceConvert(rimageType);
  reduceType = operate;
  glslPath = "glsl/reduce2_sumF4.comp.spv";
  if (this->imageType == ImageType::r32f) {
    glslPath = "glsl/reduce2_sumF1.comp.spv";
  }
}

VkReduceLayer::~VkReduceLayer() {}

void VkReduceLayer::onInitGroup() {
  inFormats[0].imageType = this->imageType;
  outFormats[0].imageType = this->imageType;
  // 添加前置处理节点
  preLayer = vkPipeGraph->addNode<VkPreReduceLayer>(reduceType, imageType);
}

void VkReduceLayer::onInitNode() {
  preLayer->addLine(getNode(), 0, 0);
  setStartNode(preLayer);
}

void VkReduceLayer::onInitLayer() {
  sizeX = 1;  // divUp(inFormats[0].width, 256);
  sizeY = 1;
  outFormats[0].width = 1;
  outFormats[0].height = 1;
}

VkAverageLuminanceThresholdLayer::VkAverageLuminanceThresholdLayer() {
  glslPath = "glsl/averageLuminanceThreshold.comp.spv";
  inCount = 2;
  outCount = 1;
  paramet = 1.0f;
  setUBOSize(sizeof(paramet), true);
  updateUBO(&paramet);
}

VkAverageLuminanceThresholdLayer::~VkAverageLuminanceThresholdLayer() {}

void VkAverageLuminanceThresholdLayer::onInitGroup() {
  inFormats[0].imageType = ImageType::r8;
  inFormats[1].imageType = ImageType::r32f;
  outFormats[0].imageType = ImageType::r8;
  luminanceLayer = vkPipeGraph->addNode<VkLuminanceLayer>();
  reduceLayer =
      vkPipeGraph->addNode<VkReduceLayer>(ReduceOperate::sum, ImageType::r8);
  luminanceLayer->addLine(reduceLayer);
}

void VkAverageLuminanceThresholdLayer::onInitNode() {
  luminanceLayer->addLine(getNode(), 0, 0);
  reduceLayer->addLine(getNode(), 0, 1);
  setStartNode(luminanceLayer);
}

}