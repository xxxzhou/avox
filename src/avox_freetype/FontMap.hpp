#pragma once

#include <memory>
#include <unordered_map>

#include "FreetypeHelper.hpp"

namespace avox {

// 按需动态加载并缓存字符字形
class FontMap {
 public:
  FontMap();
  ~FontMap();

 private:
  FT_Library ftLibrary = nullptr;
  FT_Face ftFace = nullptr;
  std::unordered_map<wchar_t, CachedGlyph> glyphCache;
  int32_t fontSize = 16;
  std::string fontName = "";

 private:
  bool cacheGlyph(wchar_t character);

 public:
  bool loadFont(const char* fontName, int32_t fontSize);
  void loadText(const std::string& text, std::vector<CachedGlyph>& glyphs);
};

}
