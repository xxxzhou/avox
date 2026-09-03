#include "VkLaplacianLayer.hpp"

namespace avox {

Vk3x3ConvolutionLayer::Vk3x3ConvolutionLayer() {
  glslPath = "glsl/filterMat3x3.comp.spv";
  setUBOSize(sizeof(paramet), true);
  paramet.row0 = {0.0f, 0.0f, 0.0f};
  paramet.row1 = {0.0f, 1.0f, 0.0f};
  paramet.row2 = {0.0f, 0.0f, 0.0f};
  updateUBO(&paramet);
}
Vk3x3ConvolutionLayer::~Vk3x3ConvolutionLayer() {}

VkLaplacianLayer::VkLaplacianLayer(bool bSmall) {
  glslPath = "glsl/filterMat3x3.comp.spv";
  setUBOSize(sizeof(Mat3x3f));
  if (bSmall) {
    mat.row0 = {0.0f, 1.0f, 0.0f};
    mat.row1 = {1.0f, -4.0f, 1.0f};
    mat.row2 = {0.0f, 1.0f, 0.0f};
  } else {
    mat.row0 = {2.0f, 0.0f, 2.0f};
    mat.row1 = {0.0f, -8.0f, 0.0f};
    mat.row2 = {2.0f, 0.0f, 2.0f};
  }
  updateUBO(&mat);
}

VkLaplacianLayer::~VkLaplacianLayer() {}

}