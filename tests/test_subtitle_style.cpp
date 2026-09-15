// 字幕文本样式单测(计划 doc/plan/player/字幕样式设计.md P1)
// 覆盖: TextCanvasStyle 默认值 = 旧行为常量; computeTextPos 默认样式代入与
// 旧底部锚点公式逐值一致; align/margin/offset 各方向语义; 帧内钳制。
// 渲染主路径(render)依赖 FontCache/freetype 资产, 归播放回归矩阵覆盖。
#include <doctest.h>

#include "avox/subtitle/TextRasterizer.hpp"

namespace {

using avox::TextCanvasStyle;
using avox::computeTextPos;
using E = avox::HAlignType;
using V = avox::VAlignType;

// 旧实现公式(迁移前 TextRasterizer.cpp 底部锚点写法), 作对照基准
static void legacyPos(const TextCanvasStyle& s, int32_t frameW, int32_t frameH,
                      int32_t totalW, int32_t totalH, int32_t* outX,
                      int32_t* outY) {
  (void)s;
  *outX = std::min(std::max(0, (int32_t)(frameW * 0.5f) - totalW / 2),
                   frameW - totalW);
  *outY = std::min(std::max(0, (int32_t)(frameH * 0.8f) - totalH),
                   frameH - totalH);
}

TEST_CASE("字幕样式: 默认值 = 旧行为常量") {
  TextCanvasStyle s;
  CHECK(s.fontName == "simhei.ttf");
  CHECK(s.fontSize == 40);
  CHECK(s.colorR == 1.f);
  CHECK(s.colorG == 1.f);
  CHECK(s.colorB == 1.f);
  CHECK(s.hAlign == E::mid);
  CHECK(s.vAlign == V::bottom);
  CHECK(s.anchorXRatio == 0.5f);
  CHECK(s.anchorYRatio == 0.8f);
  CHECK(s.marginX == 0.f);
  CHECK(s.marginY == 0.f);
  CHECK(s.maxWidthRatio == 0.8f);
  CHECK(s.scale == 1.f);
  CHECK(s.offsetXRatio == 0.f);
  CHECK(s.offsetYRatio == 0.f);
  CHECK(s.opacity == 1.f);
}

TEST_CASE("字幕样式: 默认样式落点与旧公式逐值一致") {
  const int32_t frames[][2] = {{640, 360},    {1280, 720}, {1920, 1080},
                               {3840, 2160},  {1080, 1920}};
  const int32_t blocks[][2] = {{100, 40},  {500, 120}, {1200, 300},
                               {30, 20},   {2000, 800}};
  for (auto& f : frames) {
    for (auto& b : blocks) {
      CAPTURE(f[0]);
      CAPTURE(f[1]);
      CAPTURE(b[0]);
      CAPTURE(b[1]);
      TextCanvasStyle s;  // 全默认
      int32_t x = 0, y = 0, lx = 0, ly = 0;
      computeTextPos(s, f[0], f[1], b[0], b[1], &x, &y);
      legacyPos(s, f[0], f[1], b[0], b[1], &lx, &ly);
      CHECK(x == lx);
      CHECK(y == ly);
    }
  }
}

TEST_CASE("字幕样式: left/top 对齐 + 边距内收") {
  const int32_t frameW = 1920, frameH = 1080, totalW = 400, totalH = 100;
  TextCanvasStyle s;
  s.hAlign = E::left;
  s.vAlign = V::top;
  s.anchorXRatio = 0.05f;  // setPosition(0.05, 0.05)
  s.anchorYRatio = 0.05f;
  s.marginX = 20;  // setPositionMargin(20, 20), 1080 基准随帧高缩放
  s.marginY = 20;
  int32_t x = 0, y = 0;
  computeTextPos(s, frameW, frameH, totalW, totalH, &x, &y);
  const int32_t mScaled = (int32_t)(20 * (float)frameH / 1080.f);
  CHECK(x == (int32_t)(frameW * 0.05f) + mScaled);
  CHECK(y == (int32_t)(frameH * 0.05f) + mScaled);
}

TEST_CASE("字幕样式: right/bottom 对齐 = 减边距再退块宽高") {
  const int32_t frameW = 1920, frameH = 1080, totalW = 400, totalH = 100;
  TextCanvasStyle s;
  s.hAlign = E::right;
  s.vAlign = V::bottom;
  s.anchorXRatio = 0.5f;
  s.anchorYRatio = 0.8f;
  s.marginX = 54;  // 1080 基准下正好 54px
  s.marginY = 80;
  int32_t x = 0, y = 0;
  computeTextPos(s, frameW, frameH, totalW, totalH, &x, &y);
  CHECK(x == 960 - 54 - totalW);
  CHECK(y == 864 - 80 - totalH);
}

TEST_CASE("字幕样式: mid 对齐边距不参与, top 锚点在块上方") {
  const int32_t frameW = 1280, frameH = 720, totalW = 400, totalH = 100;
  TextCanvasStyle s;
  s.vAlign = V::top;
  s.marginY = 36;
  int32_t x = 0, y = 0;
  computeTextPos(s, frameW, frameH, totalW, totalH, &x, &y);
  CHECK(x == frameW / 2 - totalW / 2);  // mid: marginX 不参与
  CHECK(y == (int32_t)(frameH * 0.8f) + (int32_t)(36 * (float)frameH / 1080.f));
}

TEST_CASE("字幕样式: offset 帧归一化平移, 叠加在钳制前") {
  const int32_t frameW = 1920, frameH = 1080, totalW = 400, totalH = 100;
  TextCanvasStyle s;
  s.offsetXRatio = 0.1f;   // 192px
  s.offsetYRatio = -0.05f;  // -54px
  int32_t x = 0, y = 0, x0 = 0, y0 = 0;
  computeTextPos(s, frameW, frameH, totalW, totalH, &x, &y);
  s.offsetXRatio = 0.f;
  s.offsetYRatio = 0.f;
  computeTextPos(s, frameW, frameH, totalW, totalH, &x0, &y0);
  CHECK(x == x0 + 192);
  CHECK(y == y0 - 54);
}

TEST_CASE("字幕样式: 块大于帧时负偏移, 由层侧 blit 钳制(同旧行为)") {
  TextCanvasStyle s;
  int32_t x = 0, y = 0;
  computeTextPos(s, 100, 100, 500, 300, &x, &y);
  // min(0, frame-total): 超宽/超高块左上角为负, 层侧 blit 裁进帧内
  CHECK(x == 100 - 500);
  CHECK(y == 100 - 300);
}

}
