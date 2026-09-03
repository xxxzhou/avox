#include "VkConvertImageLayer.hpp"

namespace avox {

VkConvertImageLayer::VkConvertImageLayer(ConvertType convert) {
  this->convert = convert;
  glslPath = "glsl/convertImage.comp.spv";
  if (convert == ConvertType::rgba32f2rgba8) {
    glslPath = "glsl/convertImageF4.comp.spv";
  } else if (convert == ConvertType::rgba16f2rgba32f) {
    glslPath = "glsl/convertImageH2F4.comp.spv";
  } else if (convert == ConvertType::rgba32f2rgba16f) {
    glslPath = "glsl/convertImageF4H.comp.spv";
  }
}

VkConvertImageLayer::~VkConvertImageLayer() {}

void VkConvertImageLayer::onInitGraph() {
  VkLayer::onInitGraph();
  inFormats[0].imageType = ImageType::rgba8;
  outFormats[0].imageType = ImageType::rgba32f;
  if (convert == ConvertType::rgba32f2rgba8) {
    inFormats[0].imageType = ImageType::rgba32f;
    outFormats[0].imageType = ImageType::rgba8;
  } else if (convert == ConvertType::rgba16f2rgba32f) {
    inFormats[0].imageType = ImageType::rgba16f;
    outFormats[0].imageType = ImageType::rgba32f;
  } else if (convert == ConvertType::rgba32f2rgba16f) {
    inFormats[0].imageType = ImageType::rgba32f;
    outFormats[0].imageType = ImageType::rgba16f;
  }
}

}
