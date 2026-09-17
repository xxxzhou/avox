#pragma once

#include "VkLayer.hpp"
#include "avox/AvoxLayer.h"

namespace avox {

// VR视角状态(渲染线程消费): rotateView/zoomView 累加钳位后的结果
struct VrViewState {
  float yaw = 0.0f;    // 度, 右正
  float pitch = 0.0f;  // 度, 上正
  float fov = 90.0f;   // 度, 垂直视野
  int32_t outMode = 0; // VrOutMode
  float stereo = 0.0f; // 立体强度(度), 双眼水平反向各偏一半, mono无效
  inline bool operator==(const VrViewState& r) const {
    return yaw == r.yaw && pitch == r.pitch && fov == r.fov &&
           outMode == r.outMode && stereo == r.stereo;
  }
  inline bool operator!=(const VrViewState& r) const { return !(*this == r); }
};

// VR几何参数(建图期定形, 变更走graph重建), 全部折算到eye局部uv:
// 眼矩形原点/宽高、鱼眼圆心/半径已从 VrParamet 的全帧坐标换算完毕
struct VrLayerGeom {
  int32_t projection = 0;   // VrProjection
  float fisheyeFov = 180.0f;
  float eyeW = 0.5f;                 // 每眼矩形宽(全帧uv)
  float eyeH = 1.0f;                 // 每眼矩形高
  float eyeLu = 0.0f, eyeLv = 0.0f;  // 左眼矩形原点(全帧uv)
  float eyeRu = 0.5f, eyeRv = 0.0f;  // 右眼矩形原点
  float cLu = 0.5f, cLv = 0.5f;      // 左眼圆心(eye局部uv)
  float cRu = 0.5f, cRv = 0.5f;      // 右眼圆心
  float rLu = 0.5f, rLv = 0.5f;      // 左眼半径(局部uv, ru≠rv时仍为圆)
  float rRu = 0.5f, rRv = 0.5f;      // 右眼半径
  int32_t outWidth = 1920;
  int32_t outHeight = 1080;
  inline bool operator==(const VrLayerGeom& r) const {
    return projection == r.projection && fisheyeFov == r.fisheyeFov &&
           eyeW == r.eyeW && eyeH == r.eyeH && eyeLu == r.eyeLu &&
           eyeLv == r.eyeLv && eyeRu == r.eyeRu && eyeRv == r.eyeRv &&
           cLu == r.cLu && cLv == r.cLv && cRu == r.cRu && cRv == r.cRv &&
           rLu == r.rLu && rLv == r.rLv && rRu == r.rRu && rRv == r.rRv &&
           outWidth == r.outWidth && outHeight == r.outHeight;
  }
  inline bool operator!=(const VrLayerGeom& r) const { return !(*this == r); }
};

// VR投影重映射层: 每像素反向映射采样(等距鱼眼/等距柱状 -> 虚拟透视相机),
// 单 pass 单采样, 输出尺寸跟随宿主设置(enableSizeChange), 与 resize 互替
class VkVrLayer : public VkLayer, public IParamet<VrLayerGeom> {
  AVOX_LAYER_GETNAME(VkVrLayer)
 public:
  VkVrLayer();
  virtual ~VkVrLayer();

 public:
  // 视角/输出模式快速通道(渲染线程调用, onParametUpdate 路径), 不重建graph
  void setViewState(const VrViewState& state);
  const VrViewState& getViewState() const { return viewState; }

 protected:
  virtual bool getSampled(int32_t inIndex) override;
  virtual void onUpdateParamet() override;
  virtual void onInitGraph() override;
  virtual void onInitLayer() override;

 private:
  // 几何+视角打包进 UBO 并置 bParametChange(onPreFrame 上传)
  void pushUbo();
  VrViewState viewState = {};
};

}
