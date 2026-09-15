#pragma once

#include <cstdint>
#include <string>
#include <vector>

#ifdef AVOX_ENABLE_FREETYPE

namespace avox {

// 文本字幕画布样式(视频 storage 分辨率坐标系), 默认值对齐旧 VkFontLayer 观感
struct TextCanvasStyle {
  const char* fontName = "simhei.ttf";  // asset/fonts 下的字体名(FontMap 解析)
  int32_t fontSize = 40;                // 1080p 参考字号, 随帧高 DPI 缩放
  float colorR = 1.0f;
  float colorG = 1.0f;
  float colorB = 1.0f;
  float anchorXRatio = 0.5f;  // 水平锚点(帧宽比例), 底部居中
  float anchorYRatio = 0.8f;  // 垂直锚点(bottom 对齐, 同旧默认块)
  float maxWidthRatio = 0.8f;  // 自动换行宽度(帧宽比例)
  int32_t hSpace = 3;          // 字间距(同旧默认)
  int32_t vSpace = 5;          // 行间距(同旧默认)
};

// 文本 → RGBA8(premultiplied) bbox 画布: 统一 canvas 通道的文本生产者
// (计划 doc/plan/player/字幕模块合并计划.md P2)。字形经 FontCache
// (avox_freetype), 排版/换行规则与旧 VkFontLayer 同源。
// 线程约定: 只在渲染线程调 render。
class TextRasterizer {
 public:
  // 渲染文本到内部画布。返回内容序号: 0=无内容(调用方清层), 内容变化时递增,
  // 文本与帧尺寸都没变则返回上次的序号(调用方零上传)。
  int32_t render(const char* text, int32_t frameW, int32_t frameH);

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
  std::vector<uint8_t> canvas;
  int32_t canvasW = 0;
  int32_t canvasH = 0;
  int32_t canvasX = 0;
  int32_t canvasY = 0;
  int32_t seq = 0;
  int32_t lastReturned = 0;
  std::string lastText;
  int32_t lastFrameW = 0;
  int32_t lastFrameH = 0;
};

}

#endif  // AVOX_ENABLE_FREETYPE
