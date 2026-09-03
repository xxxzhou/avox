#include "VkColorMatrixLayer.hpp"
#include "WrapMat.hpp"

namespace avox {

VkColorMatrixLayer::VkColorMatrixLayer(/* args */) {
  glslPath = "glsl/colorMatrix.comp.spv";
  setUBOSize(sizeof(paramet), true);
  updateUBO(&paramet);
}

VkColorMatrixLayer::~VkColorMatrixLayer() {}

VkHSBLayer::VkHSBLayer(/* args */) {
  glslPath = "glsl/colorMatrix.comp.spv";
  setUBOSize(sizeof(paramet));
  paramet.intensity = 1.0f;
  updateUBO(&paramet);
}

VkHSBLayer::~VkHSBLayer() {}

void VkHSBLayer::parametTransform() {
  updateUBO(&paramet);
  bParametChange = true;
}

void VkHSBLayer::reset() {
  paramet.mat = identMat4x4f();
  parametTransform();
}

void VkHSBLayer::rotateHue(const float& h) {
  paramet.mat = huerotateMat(paramet.mat, h);
  parametTransform();
}

void VkHSBLayer::adjustSaturation(const float& s) {
  paramet.mat = saturateMat(paramet.mat, s);
  parametTransform();
}

void VkHSBLayer::adjustBrightness(const float& b) {
  Mat4x4f scaleMat = scaleIdentMatf(vec3f(b, b, b));
  paramet.mat = paramet.mat.multiply(scaleMat);
  parametTransform();
}

VkSepiaLayer::VkSepiaLayer(/* args */) {
  glslPath = "glsl/colorMatrix.comp.spv";
  setUBOSize(sizeof(mparamet));
  mparamet.intensity = 1.0f;
  mparamet.mat.row0 = vec4f(0.3588f, 0.7044f, 0.1368f, 0.0f);
  mparamet.mat.row1 = vec4f(0.2990f, 0.5870f, 0.1140f, 0.0f);
  mparamet.mat.row2 = vec4f(0.2392f, 0.4696f, 0.0912f, 0.0f);
  mparamet.mat.row3 = vec4f(0.0f, 0.0f, 0.0f, 1.0f);
  updateUBO(&mparamet);
}

VkSepiaLayer::~VkSepiaLayer() {}

void VkSepiaLayer::onUpdateParamet() {
  if (mparamet.intensity != paramet) {
    mparamet.intensity = paramet;
    updateUBO(&mparamet);
    bParametChange = true;
  }
}

}