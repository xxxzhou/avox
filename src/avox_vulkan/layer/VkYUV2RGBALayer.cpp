#include "VkYUV2RGBALayer.hpp"

#include "VkPipeGraph.hpp"
namespace avox {

VkYUV2RGBALayer::VkYUV2RGBALayer(/* args */) { setUBOSize(sizeof(ColorYuvUBO)); }

VkYUV2RGBALayer::~VkYUV2RGBALayer() {}

void VkYUV2RGBALayer::onUpdateParamet() {
  if (paramet == oldParamet) {
    return;
  }
  resetGraph();
}

void VkYUV2RGBALayer::refreshColorMat() {
  uboData.colorMat = buildYuvToRgb(cs);
  updateUBO(&uboData);
}

void VkYUV2RGBALayer::setColorSpace(const ColorSpaceDesc& c) {
  cs = c;
  refreshColorMat();
  // 运行时重传 UBO, 不触发 graph 重建
  bParametChange = true;
}

void VkYUV2RGBALayer::onInitLayer() {
  YuvType yuvType = paramet;
  // nv12/yuv420P/yuy2P
  std::string path = "glsl/yuv2rgbaV1.comp.spv";
  if (yuvType == YuvType::yuv2I || yuvType == YuvType::yvyuI ||
      yuvType == YuvType::uyvyI) {
    path = "glsl/yuv2rgbaV2.comp.spv";
  }
  if (yuvType == YuvType::uyvy422_10B) {
    path = "glsl/yuv2rgbaV3.comp.spv";
  }
  if (yuvType == YuvType::yuv420P10) {
    path = "glsl/yuv2rgbaV4.comp.spv";
  }
  shader->loadShaderModule(path);
  assert(shader->shaderStage.module != VK_NULL_HANDLE);
  // 带P/SP的格式由r8转rgba8
  inFormats[0].imageType = ImageType::r8;
  outFormats[0].imageType = ImageType::rgba8;
  if (paramet == YuvType::nv12 || paramet == YuvType::yuv420P) {
    outFormats[0].height = inFormats[0].height * 2 / 3;
    // 一个线程处理四个点
    sizeX = divUp(outFormats[0].width, 2 * groupX);
    sizeY = divUp(outFormats[0].height, 2 * groupY);
  } else if (paramet == YuvType::yuv420P10) {
    // yuv420P10是平面格式,使用r16,高度是1.5倍
    inFormats[0].imageType = ImageType::r16;
    outFormats[0].height = inFormats[0].height * 2 / 3;
    // 一个线程处理四个点
    sizeX = divUp(outFormats[0].width, 2 * groupX);
    sizeY = divUp(outFormats[0].height, 2 * groupY);
  } else if (paramet == YuvType::yuv422P ||
             paramet == YuvType::yuyv422A) {
    outFormats[0].height = inFormats[0].height / 2;
    // 一个线程处理二个点
    sizeX = divUp(outFormats[0].width, 2 * groupX);
    if (paramet == YuvType::yuv422P) {
      sizeY = divUp(outFormats[0].height, groupY);
    } else {
      sizeY = divUp(outFormats[0].height, 2 * groupY);
    }
  } else if (paramet == YuvType::yuv2I || paramet == YuvType::yvyuI ||
             paramet == YuvType::uyvyI) {
    inFormats[0].imageType = ImageType::rgba8;
    // 一个线程处理二个点,yuyv四点组合成一个元素,和rgba类似
    outFormats[0].width = inFormats[0].width * 2;
    sizeX = divUp(inFormats[0].width, groupX);
    sizeY = divUp(inFormats[0].height, groupY);
  } else if (paramet == YuvType::uyvy422_10B) {
    // 一个线程处理二个点,对应yuv422_10B中的五个字节
    outFormats[0].width = inFormats[0].width * 2 / 5;
    sizeX = divUp(outFormats[0].width, 2 * groupX);
    sizeY = divUp(outFormats[0].height, groupY);
  }
  // 填 UBO 头字段 + 矩阵(随 cs)
  uboData.width = outFormats[0].width;
  uboData.height = outFormats[0].height;
  uboData.yuvType = (int32_t)yuvType;
  uboData._pad = 0;
  refreshColorMat();
}

}
