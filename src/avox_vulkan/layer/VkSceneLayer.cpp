#include "VkSceneLayer.hpp"

#include <algorithm>

#include "avox/module/LogHelper.hpp"

namespace avox {

namespace {
// 涟漪生命周期(秒)与最大半径(画布高占比基准)
constexpr float kRippleLifeSec = 0.6f;
constexpr float kRippleMaxRadiusFrac = 0.12f;
}  // namespace

VkSceneLayer::VkSceneLayer() {
  inCount = 2;  // 0=屏幕内容, 1=光标图集
  glslPath = "glsl/scene.comp.spv";
  setUBOSize(sizeof(SceneUbo));
}

VkSceneLayer::~VkSceneLayer() {}

bool VkSceneLayer::getSampled(int32_t inIndex) {
  // 内容裁切映射与图集格子采样都走硬件双线性 sampler 通道
  (void)inIndex;
  return true;
}

void VkSceneLayer::onInitGraph() {
  inFormats[0].imageType = ImageType::rgba8;
  inFormats[1].imageType = ImageType::rgba8;
  outFormats[0].imageType = ImageType::rgba8;
  VkLayer::onInitGraph();
}

void VkSceneLayer::onInitLayer() {
  if (canvasW <= 0 || canvasH <= 0) {
    log(LogLevel::error, "scene canvas not set");
    bShaderMissing = true;
    return;
  }
  outFormats[0].width = canvasW;
  outFormats[0].height = canvasH;
  sizeX = divUp(outFormats[0].width, groupX);
  sizeY = divUp(outFormats[0].height, groupY);
}

void VkSceneLayer::bindScene(SceneSource* scene_, int32_t w, int32_t h) {
  scene = scene_;
  canvasW = w;
  canvasH = h;
  // UINT64_MAX = 强制首个 evaluate 拉快照(EditorScene 初始 gen=0, 比对永假)
  snapGen = UINT64_MAX;
}

void VkSceneLayer::evaluate(int64_t ptsMs) {
  // 代数变化才换快照(EditorScene set* 侧拷贝, 这里整读)
  if (scene) {
    uint64_t gen = scene->generation();
    if (gen != snapGen) {
      SceneSnapshot next;
      if (scene->loadSnapshot(next)) {
        snap = next;
        snapGen = gen;
        static const bool dbg = std::getenv("AVOX_SCENE_DBG") != nullptr;
        if (dbg) {
          fprintf(stderr, "[scene] snapshot loaded gen=%llu padding=%.3f "
                          "canvas=%dx%d segs=%d\n",
                  (unsigned long long)snapGen, snap.style.padding, snap.canvasW,
                  snap.canvasH, (int32_t)snap.segments.size());
        }
      }
    }
  }
  evalAndPushUbo(ptsMs);
}

SceneCameraSegment VkSceneLayer::segmentAt(int64_t ptsMs) const {
  // 空表=全帧; 越界前后段延伸; v1 直切(easeIn/easeOut 预留)
  if (snap.segments.empty()) {
    return {};
  }
  const SceneCameraSegment* seg = &snap.segments[0];
  for (const auto& s : snap.segments) {
    if (ptsMs >= s.t0 && (s.t1 <= 0 || ptsMs < s.t1)) {
      seg = &s;
      break;
    }
    if (ptsMs >= s.t0) {
      seg = &s;
    }
  }
  return *seg;
}

void VkSceneLayer::evalAndPushUbo(int64_t ptsMs) {
  const int32_t srcW = inFormats.size() > 0 ? inFormats[0].width : 0;
  const int32_t srcH = inFormats.size() > 0 ? inFormats[0].height : 0;
  if (canvasW <= 0 || canvasH <= 0 || srcW <= 0 || srcH <= 0) {
    return;
  }
  SceneUbo ubo = {};
  const SceneStyle& st = snap.style;
  ubo.outMisc[0] = (float)canvasW;
  ubo.outMisc[1] = (float)canvasH;
  ubo.outMisc[2] = st.cornerRadius * canvasW;
  ubo.outMisc[3] = st.shadowRadius * canvasW;
  ubo.shadowMisc[0] = st.shadowAlpha;
  ubo.shadowMisc[1] = st.shadowOffsetX * canvasW;
  ubo.shadowMisc[2] = st.shadowOffsetY * canvasW;
  for (int32_t i = 0; i < 3; i++) {
    ubo.bgTop[i] = st.bgColorTop[i];
    ubo.bgBottom[i] = st.bgColorBottom[i];
  }
  // 镜头裁切(空表=全帧), 钳到源内
  SceneCameraSegment seg = segmentAt(ptsMs);
  float cw = seg.w > 0 ? seg.w : (float)srcW;
  float ch = seg.h > 0 ? seg.h : (float)srcH;
  float cx = std::max(seg.x, 0.0f);
  float cy = std::max(seg.y, 0.0f);
  cw = std::min(cw, (float)srcW - cx);
  ch = std::min(ch, (float)srcH - cy);
  if (cw < 2.0f || ch < 2.0f) {
    cx = 0.0f;
    cy = 0.0f;
    cw = (float)srcW;
    ch = (float)srcH;
  }
  ubo.cropRect[0] = cx;
  ubo.cropRect[1] = cy;
  ubo.cropRect[2] = cw;
  ubo.cropRect[3] = ch;
  ubo.srcMisc[0] = (float)srcW;
  ubo.srcMisc[1] = (float)srcH;
  // ≥2x 降采样开 4-tap tent
  float scaleX = cw / (float)canvasW;
  float scaleY = ch / (float)canvasH;
  ubo.srcMisc[2] = (scaleX >= 2.0f || scaleY >= 2.0f) ? 1.0f : 0.0f;
  // 内容区: 留白内按源比例适配并居中
  float pad = st.padding * canvasW;
  float availW = (float)canvasW - pad * 2.0f;
  float availH = (float)canvasH - pad * 2.0f;
  if (availW < 2.0f || availH < 2.0f) {
    pad = 0.0f;
    availW = (float)canvasW;
    availH = (float)canvasH;
  }
  float fit = std::min(availW / cw, availH / ch);
  float contW = cw * fit;
  float contH = ch * fit;
  ubo.canvasRect[0] = ((float)canvasW - contW) * 0.5f;
  ubo.canvasRect[1] = ((float)canvasH - contH) * 0.5f;
  ubo.canvasRect[2] = contW;
  ubo.canvasRect[3] = contH;
  // 源→画布映射(光标/涟漪共用)
  float mapX = contW / cw;
  float mapY = contH / ch;
  // 光标: 最近样本(t<=pts 且可见), 无样本/无图集=不画
  float nowSec = (float)((double)ptsMs / 1000.0);
  ubo.cursorMisc[0] = 0.0f;
  ubo.cursorMisc[1] = nowSec;
  int32_t cellN = snap.atlasCols > 0 && snap.atlasRows > 0
                      ? snap.atlasCols * snap.atlasRows
                      : 0;
  if (cellN > 0 && !snap.cursor.empty()) {
    const SceneCursorSample* cur = nullptr;
    for (const auto& s : snap.cursor) {
      if (s.t <= ptsMs && s.visible) {
        cur = &s;
      }
    }
    if (cur) {
      // v1 取格子正方形近似; 非方形格子按图集整图纵横比修正(P2 精修)
      float cellW = 1.0f / (float)snap.atlasCols;
      float cellH = 1.0f / (float)snap.atlasRows;
      int32_t shape = std::min(std::max(cur->shape, 0), cellN - 1);
      int32_t scx = shape % snap.atlasCols;
      int32_t scy = shape / snap.atlasCols;
      ubo.cursorUv[0] = (float)scx * cellW;
      ubo.cursorUv[1] = (float)scy * cellH;
      ubo.cursorUv[2] = ubo.cursorUv[0] + cellW;
      ubo.cursorUv[3] = ubo.cursorUv[1] + cellH;
      // 源像素→画布, 图标左上角对点位
      float cpx = (cur->x - cx) * mapX + ubo.canvasRect[0];
      float cpy = (cur->y - cy) * mapY + ubo.canvasRect[1];
      float drawH = st.cursorScale * (float)canvasH;
      float drawW = drawH * (cellW * (float)snap.atlasCols) /
                    (cellH * (float)snap.atlasRows);
      ubo.cursorRect[0] = cpx;
      ubo.cursorRect[1] = cpy;
      ubo.cursorRect[2] = drawW;
      ubo.cursorRect[3] = drawH;
      ubo.cursorMisc[0] = 1.0f;
    }
  }
  // 涟漪: 生命周期内的点击, 最多 4 个(取最近)
  int32_t rippleN = 0;
  for (auto it = snap.clicks.rbegin();
       it != snap.clicks.rend() && rippleN < 4; ++it) {
    float age = nowSec - (float)((double)it->t / 1000.0);
    if (age < 0.0f || age > kRippleLifeSec || it->strength <= 0.0f) {
      continue;
    }
    float rpx = (it->x - cx) * mapX + ubo.canvasRect[0];
    float rpy = (it->y - cy) * mapY + ubo.canvasRect[1];
    ubo.ripples[rippleN][0] = rpx;
    ubo.ripples[rippleN][1] = rpy;
    ubo.ripples[rippleN][2] = (float)((double)it->t / 1000.0);
    ubo.ripples[rippleN][3] =
        kRippleMaxRadiusFrac * (float)canvasH * it->strength;
    rippleN++;
  }
  updateUBO(&ubo);
  bParametChange = true;
}

}
