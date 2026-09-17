#include "VkVrLayer.hpp"

#include "VkPipeGraph.hpp"

namespace avox {

// 与 vrProject.comp 的 UBO 布局严格对应(std140, 6×vec4)
struct VrUbo {
  float view[4];  // yawDeg, pitchDeg, fovDeg(垂直), outMode
  float lens[4];  // fisheyeFovDeg, mapSpanDeg, projMode(0鱼眼,1等距柱状), outAspect
  float rect[4];  // 每眼矩形宽高(全帧uv), 0, 0
  float eyeL[4];  // 左眼 u0, v0, 圆心cx, cy(局部)
  float eyeR[4];  // 右眼 u0, v0, 圆心cx, cy
  float radii[4]; // 左眼ru,rv, 右眼ru,rv
};

VkVrLayer::VkVrLayer() {
  glslPath = "glsl/vrProject.comp.spv";
  setUBOSize(sizeof(VrUbo));
}

VkVrLayer::~VkVrLayer() {}

bool VkVrLayer::getSampled(int32_t inIndex) {
  // 反向映射需要任意uv采样, 走硬件双线性 sampler 通道
  if (inIndex == 0) {
    return true;
  }
  return false;
}

void VkVrLayer::onUpdateParamet() {
  // 几何变更(投影模式/眼布局/圆参数/输出尺寸)才重建; 视角走 setViewState
  if (paramet == oldParamet) {
    return;
  }
  resetGraph();
}

void VkVrLayer::onInitGraph() {
  inFormats[0].imageType = ImageType::rgba8;
  outFormats[0].imageType = ImageType::rgba8;
  VkLayer::onInitGraph();
}

void VkVrLayer::onInitLayer() {
  assert(paramet.outWidth > 0 && paramet.outHeight > 0);
  outFormats[0].width = paramet.outWidth;
  outFormats[0].height = paramet.outHeight;
  pushUbo();
  sizeX = divUp(outFormats[0].width, groupX);
  sizeY = divUp(outFormats[0].height, groupY);
}

void VkVrLayer::setViewState(const VrViewState& state) {
  if (viewState == state) {
    return;
  }
  viewState = state;
  pushUbo();
}

void VkVrLayer::pushUbo() {
  VrUbo ubo = {};
  ubo.view[0] = viewState.yaw;
  ubo.view[1] = viewState.pitch;
  ubo.view[2] = viewState.fov;
  ubo.view[3] = (float)viewState.outMode;
  bool bEquirect = paramet.projection >= (int32_t)VrProjection::equirect180;
  ubo.lens[0] = paramet.fisheyeFov;
  // 等距柱状横向跨角: equirect180=180°, equirect360=360°
  ubo.lens[1] = paramet.projection == (int32_t)VrProjection::equirect180 ? 180.0f : 360.0f;
  ubo.lens[2] = bEquirect ? 1.0f : 0.0f;
  ubo.lens[3] = (float)paramet.outWidth / (float)paramet.outHeight;
  ubo.rect[0] = paramet.eyeW;
  ubo.rect[1] = paramet.eyeH;
  ubo.eyeL[0] = paramet.eyeLu;
  ubo.eyeL[1] = paramet.eyeLv;
  ubo.eyeL[2] = paramet.cLu;
  ubo.eyeL[3] = paramet.cLv;
  ubo.eyeR[0] = paramet.eyeRu;
  ubo.eyeR[1] = paramet.eyeRv;
  ubo.eyeR[2] = paramet.cRu;
  ubo.eyeR[3] = paramet.cRv;
  ubo.radii[0] = paramet.rLu;
  ubo.radii[1] = paramet.rLv;
  ubo.radii[2] = paramet.rRu;
  ubo.radii[3] = paramet.rRv;
  updateUBO(&ubo);
  bParametChange = true;
}

}
