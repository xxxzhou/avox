#include "FontCache.hpp"

namespace avox {

FontCache& FontCache::instance() {
  static FontCache inst;
  return inst;
}

FontCache::FontCache() {
  setFont("simhei.ttf", 32);
}

bool FontCache::setFont(const std::string& fontName, int32_t fontSize) {
  std::lock_guard<std::mutex> lock(mtx);
  std::string key = fontName + "_" + std::to_string(fontSize);
  auto it = fontMaps.find(key);
  if (it == fontMaps.end()) {
    auto fontMap = std::make_unique<FontMap>();
    if (!fontMap->loadFont(fontName.c_str(), fontSize)) {
      return false;
    }
    it = fontMaps.emplace(key, std::move(fontMap)).first;
  }
  current = it->second.get();
  return true;
}

void FontCache::loadText(const std::string& text,
                         std::vector<CachedGlyph>& glyphs) {
  std::lock_guard<std::mutex> lock(mtx);
  if (!current) return;
  current->loadText(text, glyphs);
}

}
