#pragma once

#include <vector>

#include "VkLayer.hpp"
#include "avox/AvoxCapture.h"

namespace avox {

// 场景快照(控制侧 EditorScene 写, 渲染线程整读; gen=代数, 变了才换)
struct SceneSnapshot {
  SceneStyle style = {};
  int32_t canvasW = 0;
  int32_t canvasH = 0;
  std::vector<SceneCameraSegment> segments;
  std::vector<SceneCursorSample> cursor;
  std::vector<SceneClick> clicks;
  int32_t atlasCols = 0;
  int32_t atlasRows = 0;
  uint64_t gen = 0;
};

// 场景数据源(渲染侧视图): EditorScene 实现; 与公共 IEditorScene 分离,
// 快照结构不进公共头。VkVideoRender 持公共 IEditorScene*, 建图时 cross-cast
class SceneSource {
 public:
  virtual ~SceneSource() = default;

 public:
  virtual uint64_t generation() = 0;
  virtual bool loadSnapshot(SceneSnapshot& out) = 0;
  virtual void canvas(int32_t& w, int32_t& h) = 0;
  virtual IImageBuffer* getAtlas() = 0;
};

// 与 scene.comp 的 UBO 布局严格对应(std140, 10 个 vec4 + ripples[4] = 14×vec4)
struct SceneUbo {
  float outMisc[4];     // outW, outH, radiusPx, shadowRadiusPx
  float canvasRect[4];  // 内容区 x, y, w, h(画布像素)
  float shadowMisc[4];  // shadowAlpha, offX(px), offY(px), 0
  float bgTop[4];
  float bgBottom[4];
  float cropRect[4];    // 源像素 x, y, w, h(当前镜头)
  float srcMisc[4];     // srcW, srcH, bTent(降采样≥2x), 0
  float cursorRect[4];  // 画布像素 x, y, w, h
  float cursorUv[4];    // 图集格子 u0, v0, u1, v1
  float cursorMisc[4];  // cursorAlpha, nowSec, 0, 0
  float ripples[4][4];  // 每格: cx, cy(画布px), t0Sec, maxR(px); w<=0 无效
};

// 场景合成层(2 入 1 出单 pass): 0=屏幕内容(yuv2RGBA 后 rgba8), 1=光标图集。
// 绘制顺序: 背景渐变 → 圆角阴影 → cropRect 采样内容(≥2x 降采样 4-tap tent)
// → 圆角 mask → 光标精灵 → 点击涟漪。输出=画布尺寸, 与 resize 互替。
// 求值在渲染线程 evaluate(pts): 查镜头段(直切)/光标最近样本/0.6s 涟漪
class VkSceneLayer : public VkLayer {
  AVOX_LAYER_GETNAME(VkSceneLayer)

 public:
  VkSceneLayer();
  virtual ~VkSceneLayer();

 public:
  // 建图时绑定: 数据源+画布尺寸(canvas<=0 建图失败); 不持宿主指针生命周期
  void bindScene(SceneSource* scene, int32_t canvasW, int32_t canvasH);
  // 每帧渲染前(pts 毫秒): 代数换快照 + 求值 + 写 UBO(下一拍上传)
  void evaluate(int64_t ptsMs);

 protected:
  virtual bool getSampled(int32_t inIndex) override;
  virtual void onInitGraph() override;
  virtual void onInitLayer() override;

 private:
  // 按当前快照/pts 求值并填 UBO
  void evalAndPushUbo(int64_t ptsMs);
  // 镜头段查表(空表=全帧; t1<=0 延伸到尾; v1 直切)
  SceneCameraSegment segmentAt(int64_t ptsMs) const;

  SceneSource* scene = nullptr;
  int32_t canvasW = 0;
  int32_t canvasH = 0;
  SceneSnapshot snap;
  uint64_t snapGen = UINT64_MAX;
};

}
