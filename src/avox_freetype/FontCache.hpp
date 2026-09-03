#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "FontMap.hpp"

namespace avox {

// 全局单例字体缓存，按 fontName_fontSize 缓存 FontMap
// loadText 内部加锁，支持多线程安全访问
class FontCache {
 public:
  // 获取单例
  static FontCache& instance();

  // 禁止拷贝/移动
  FontCache(const FontCache&) = delete;
  FontCache& operator=(const FontCache&) = delete;

  // 切换字体，缓存命中直接使用，未命中则加载并缓存
  bool setFont(const std::string& fontName, int32_t fontSize);
  bool hasCurrent() const { return current != nullptr; }

  // 线程安全的文本加载
  void loadText(const std::string& text, std::vector<CachedGlyph>& glyphs);

 private:
  FontCache();

  std::mutex mtx;
  // key: "fontName_fontSize" → FontMap
  std::unordered_map<std::string, std::unique_ptr<FontMap>> fontMaps;
  FontMap* current = nullptr;
};

}
