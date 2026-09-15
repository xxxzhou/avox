#include "TextRasterizer.hpp"

#ifdef AVOX_ENABLE_FREETYPE

#include <algorithm>

#include "avox_freetype/FontCache.hpp"

namespace avox {

// DPI 参考高度(与 VkFontLayer 同源): 字号随帧高线性缩放
static constexpr int32_t kReferenceHeight = 1080;

// 按显式换行拆行(UTF-8 多字节不含 0x0A, 按字节切安全)
static void splitLines(const std::string& text,
                       std::vector<std::string>& out) {
  size_t start = 0;
  while (start <= text.size()) {
    size_t pos = text.find('\n', start);
    if (pos == std::string::npos) {
      out.emplace_back(text.substr(start));
      break;
    }
    out.emplace_back(text.substr(start, pos - start));
    start = pos + 1;
  }
}

int32_t TextRasterizer::render(const char* text, int32_t frameW,
                               int32_t frameH) {
  if (!text || text[0] == '\0' || frameW <= 0 || frameH <= 0) {
    lastReturned = 0;
    return 0;
  }
  // 文本与帧尺寸都没变: 内容没变, 序号不变(调用方零上传)
  if (lastText == text && frameW == lastFrameW && frameH == lastFrameH) {
    return lastReturned;
  }
  lastText = text;
  lastFrameW = frameW;
  lastFrameH = frameH;

  auto& fontCache = FontCache::instance();
  // 字号随帧高 DPI 缩放, 与旧 VkFontLayer 观感对齐
  const int32_t fontPx =
      std::max(8, (int32_t)(style.fontSize * (float)frameH / kReferenceHeight));
  if (!fontCache.setFont(style.fontName, fontPx)) {
    lastReturned = 0;
    return 0;
  }

  // 拆行 → 逐行取字形 → 超宽自动换行(规则同旧 computeLayout)
  const int32_t maxW =
      std::max(1, (int32_t)(frameW * style.maxWidthRatio));
  struct VisualLine {
    int32_t width = 0;
    int32_t height = 0;
    std::vector<CachedGlyph> glyphs;
  };
  std::vector<VisualLine> lines;
  std::vector<std::string> srcLines;
  splitLines(text, srcLines);
  for (auto& sl : srcLines) {
    std::vector<CachedGlyph> glyphs;
    fontCache.loadText(sl, glyphs);
    VisualLine cur;
    for (auto& g : glyphs) {
      if (g.width <= 0 || g.height <= 0) {
        continue;  // 空白/缺字形
      }
      const int32_t adv = g.width + (cur.glyphs.empty() ? 0 : style.hSpace);
      if (!cur.glyphs.empty() && cur.width + adv > maxW) {
        lines.push_back(std::move(cur));
        cur = VisualLine{};
      }
      cur.width += g.width + (cur.glyphs.empty() ? 0 : style.hSpace);
      cur.height = std::max(cur.height, g.height);
      cur.glyphs.push_back(std::move(g));
    }
    if (!cur.glyphs.empty()) {
      lines.push_back(std::move(cur));
    }
  }
  if (lines.empty()) {
    lastReturned = 0;
    return 0;
  }

  // 画布尺寸 = 联合 bbox(行内居中, 高度含行距, 同旧 totalHeight 口径)
  int32_t totalW = 0;
  int32_t totalH = 0;
  for (auto& ln : lines) {
    totalW = std::max(totalW, ln.width);
    totalH += ln.height;
  }
  totalH += (int32_t)lines.size() * style.vSpace;

  // RGBA8 premultiplied, max-blend(与旧 R8 canvas 语义一致)
  canvas.assign((size_t)totalW * totalH * 4, 0);
  canvasW = totalW;
  canvasH = totalH;
  int32_t curY = 0;
  for (auto& ln : lines) {
    int32_t curX = (totalW - ln.width) / 2;
    for (auto& g : ln.glyphs) {
      const int32_t baseY = ln.height - g.baseline;
      for (int32_t y = 0; y < g.height; ++y) {
        const int32_t dy = curY + baseY + y;
        if (dy < 0 || dy >= totalH) {
          continue;
        }
        const uint8_t* src = &g.bitmap[(size_t)y * g.width];
        uint8_t* dst = &canvas[(size_t)dy * totalW * 4];
        for (int32_t x = 0; x < g.width; ++x) {
          const uint8_t a = src[x];
          if (a == 0) {
            continue;
          }
          uint8_t* px = dst + (size_t)(curX + x) * 4;
          if (a > px[3]) {
            px[0] = (uint8_t)(style.colorR * a);
            px[1] = (uint8_t)(style.colorG * a);
            px[2] = (uint8_t)(style.colorB * a);
            px[3] = a;
          }
        }
      }
      curX += g.width + style.hSpace;
    }
    curY += style.vSpace + ln.height;
  }

  // 底部锚点定位(帧坐标系), 越界裁到帧内
  canvasX = std::min(std::max(0, (int32_t)(frameW * style.anchorXRatio) - totalW / 2),
                     frameW - totalW);
  canvasY = std::min(std::max(0, (int32_t)(frameH * style.anchorYRatio) - totalH),
                     frameH - totalH);
  ++seq;
  lastReturned = seq;
  return seq;
}

}

#endif  // AVOX_ENABLE_FREETYPE
