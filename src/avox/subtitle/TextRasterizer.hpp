#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "../AvoxDef.h"
#include "../AvoxLayer.h"

#ifdef AVOX_ENABLE_FREETYPE

namespace avox {

// 文本字幕画布样式(视频 storage 分辨率坐标系), 默认值对齐旧 VkFontLayer 观感。
// fontSize/margin 为 1080 基准(随帧高缩放); scale/offset/opacity 为全局变换,
// 文本路径在 CPU 侧消费(重栅格化, 清晰), 不再下发给 canvas 合成层
struct TextCanvasStyle {
  std::string fontName = "simhei.ttf";  // asset/fonts 下的字体名(FontMap 解析)
  int32_t fontSize = 40;                // 1080p 参考字号, 随帧高 DPI 缩放
  float colorR = 1.0f;
  float colorG = 1.0f;
  float colorB = 1.0f;
  HAlignType hAlign = HAlignType::mid;  // 水平对齐(默认 = 旧行为: 锚点居中)
  VAlignType vAlign = VAlignType::bottom;  // 垂直对齐(默认 = 旧行为: 锚点之上)
  float anchorXRatio = 0.5f;  // 水平锚点(帧宽比例), 底部居中
  float anchorYRatio = 0.8f;  // 垂直锚点(bottom 对齐, 同旧默认块)
  float marginX = 0.f;        // 对齐方向内收边距(1080 基准像素)
  float marginY = 0.f;
  float maxWidthRatio = 0.8f;  // 自动换行宽度(帧宽比例)
  int32_t hSpace = 3;          // 字间距(同旧默认)
  int32_t vSpace = 5;          // 行间距(同旧默认)
  // ---- 全局变换(CPU 侧消费) ----
  float scale = 1.f;  // 字号倍率(<=0 按 1 处理)
  float offsetXRatio = 0.f;  // 帧归一化平移
  float offsetYRatio = 0.f;
  float opacity = 1.f;  // 写 alpha 时乘入(premultiplied), <=0 无内容
};

// 排版落点合成(帧坐标系): align/margin/anchor/offset → 画布 bbox 左上角,
// 帧内钳制。margin 按对齐方向内收(left 加在左/right 减在右/mid 不参与;
// top 加在锚点下/bottom 减在锚点上/mid 不参与), offset 在钳制前叠加。
// 默认样式代入即旧公式: x = anchorX*frameW - totalW/2, y = anchorY*frameH - totalH
inline void computeTextPos(const TextCanvasStyle& s, int32_t frameW,
                           int32_t frameH, int32_t totalW, int32_t totalH,
                           int32_t* outX, int32_t* outY) {
  const float dpi = (float)frameH / 1080.f;
  const int32_t mX = (int32_t)(s.marginX * dpi);
  const int32_t mY = (int32_t)(s.marginY * dpi);
  const int32_t ax = (int32_t)(frameW * s.anchorXRatio);
  const int32_t ay = (int32_t)(frameH * s.anchorYRatio);
  int32_t startX = ax;
  switch (s.hAlign) {
    case HAlignType::left: startX = ax + mX; break;
    case HAlignType::right: startX = ax - mX - totalW; break;
    default: startX = ax - totalW / 2; break;  // mid
  }
  int32_t startY = ay;
  switch (s.vAlign) {
    case VAlignType::top: startY = ay + mY; break;
    case VAlignType::mid: startY = ay - totalH / 2; break;
    default: startY = ay - mY - totalH; break;  // bottom
  }
  const int32_t x = startX + (int32_t)(frameW * s.offsetXRatio);
  const int32_t y = startY + (int32_t)(frameH * s.offsetYRatio);
  // (std::min)/(std::max) 加括号: 防 Windows.h 的 min/max 宏污染(sample TU 直包含)
  *outX = (std::min)((std::max)(0, x), frameW - totalW);
  *outY = (std::min)((std::max)(0, y), frameH - totalH);
}

// 文本 → RGBA8(premultiplied) bbox 画布: 统一 canvas 通道的文本生产者
// (计划 doc/plan/player/字幕模块合并计划.md P2)。字形经 FontCache
// (avox_freetype), 排版/换行规则与旧 VkFontLayer 同源。
// 线程约定: setStyle 与 render 都只在渲染线程调(调用方自行做好样式快照)。
class AVOX_EXPORT TextRasterizer {
 public:
  // 渲染文本到内部画布。返回内容序号: 0=无内容(调用方清层), 内容变化时递增,
  // 文本/帧尺寸/样式版本都没变则返回上次的序号(调用方零上传)。
  int32_t render(const char* text, int32_t frameW, int32_t frameH);

  // 灌样式(权威副本 + 版本号): 版本号参与 render 缓存判定(样式变了必重绘)
  void setStyle(const TextCanvasStyle& s, int32_t seq) {
    style = s;
    styleSeq = seq;
  }

  // 最近一次 render 的画布(下次 render 前有效)
  const uint8_t* rgba() const { return canvas.empty() ? nullptr : canvas.data(); }
  int32_t width() const { return canvasW; }
  int32_t height() const { return canvasH; }
  int32_t stride() const { return canvasW * 4; }
  int32_t offsetX() const { return canvasX; }
  int32_t offsetY() const { return canvasY; }
  TextCanvasStyle& getStyle() { return style; }

 private:
  TextCanvasStyle style;
  int32_t styleSeq = 0;
  std::vector<uint8_t> canvas;
  int32_t canvasW = 0;
  int32_t canvasH = 0;
  int32_t canvasX = 0;
  int32_t canvasY = 0;
  int32_t seq = 0;
  int32_t lastReturned = 0;
  std::string lastText;
  int32_t lastStyleSeq = 0;
  int32_t lastFrameW = 0;
  int32_t lastFrameH = 0;
};

}

#endif  // AVOX_ENABLE_FREETYPE
