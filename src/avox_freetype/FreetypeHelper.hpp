#pragma once

#include "FreetypeExport.h"
#include "avox/module/LogHelper.hpp"
#include "freetype/freetype.h"

namespace avox {

// 每个字符在纹理的UV/Size（atlas 方案，重构后不再使用 x/y）
struct GlyphInfo {
  int32_t x = 0;
  int32_t y = 0;
  int32_t width = 0;
  int32_t height = 0;
  int32_t baseline = 0;
};

// CachedGlyph：按需缓存单个字符的 bitmap，替代 atlas 方案
struct CachedGlyph {
  int32_t width = 0;
  int32_t height = 0;
  int32_t baseline = 0;
  std::vector<uint8_t> bitmap;  // 拷贝的像素数据，R8，行优先
  // bitmap[y * width + x] = alpha
};

// 排版文本行信息结构体
struct TextLine {
  // 该行在 glyphs 数组中的起始索引
  size_t startGlyphIndex = 0;
  // 该行的字符数量
  size_t glyphCount = 0;
  // 该行的总宽度
  int32_t width = 0;
  // 该行的最大高度
  int32_t height = 0;
};

// fontName对应asset/fonts/下面的字体文件名
bool loadFontFace(const char *fontName, const FT_Library &ftLib, FT_Face &ftFace);

}