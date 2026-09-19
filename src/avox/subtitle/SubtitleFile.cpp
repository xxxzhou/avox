#include "SubtitleFile.hpp"

#include <cstring>
#include <fstream>

#include "../module/LogHelper.hpp"
#include "CharsetConvert.hpp"

namespace avox {

SubtitleFile::SubtitleFile() { }

SubtitleFile::~SubtitleFile() { }

bool SubtitleFile::loadFile(const char* path) {
  if (!path) return false;

#if defined(_WIN32)
  // MSVC 扩展直收宽路径: UTF-8 路径经 utf8ToWide 与 ACP 无关(中文路径自愈)
  std::ifstream file(utf8ToWide(path), std::ios::binary | std::ios::ate);
#else
  std::ifstream file(path, std::ios::binary | std::ios::ate);
#endif
  if (!file.is_open()) return false;

  size_t size = file.tellg();
  if (size == 0) return false;

  file.seekg(0, std::ios::beg);
  std::string content;
  content.resize(size);
  file.read(&content[0], size);
  file.close();
  // 编码归一: 外挂字幕常见 GBK(非 UTF-8), 直接按 UTF-8 走会被 TextRasterizer
  // 判成查不到字形 → 整屏无字。这里转成 UTF-8 再解析(UTF-8 则只剥 BOM)
  std::string text;
  encoding = normalizeSubtitleText(content, text);
  if (encoding == SubtitleEncoding::gbkTranscoded) {
    LOGFLF(LogLevel::info, "subtitle text transcoded to UTF-8, encoding=gbk: ",
           path);
  } else if (encoding == SubtitleEncoding::unknown &&
             !isUtf8Text(text.c_str(), text.size())) {
    LOGFLF(LogLevel::warn,
           "subtitle text is not UTF-8 and cannot be converted on this "
           "platform: ",
           path);
  }
  // Parse SRT
  std::vector<SrtParser::RawItem> rawItems;
  SrtParser::parse(text.c_str(), text.size(), rawItems);
  // Convert to stored format
  items.clear();
  for (const auto& raw : rawItems) {
    SubtitleItem item;
    item.startMs = raw.startMs;
    item.endMs = raw.endMs;
    item.text = raw.text;
    items.push_back(item);
  }
  bool bHave = !items.empty();  
  return bHave;
}

const SubtitleItem* SubtitleFile::getCurrent(int64_t ptsMs) {
  if (items.empty()) {
    return nullptr;
  }
  // Binary search for current subtitle
  int32_t left = 0;
  int32_t right = static_cast<int32_t>(items.size()) - 1;
  while (left <= right) {
    int32_t mid = left + (right - left) / 2;
    auto& item = items[mid];
    if (ptsMs >= item.startMs && ptsMs <= item.endMs) {
      currentIndex = mid;
      return &item;
    } else if (ptsMs < item.startMs) {
      right = mid - 1;
    } else {
      left = mid + 1;
    }
  }
  return nullptr;
}

void SubtitleFile::clear() {
  items.clear();
  currentIndex = -1;
  encoding = SubtitleEncoding::unknown;
}

bool SubtitleFile::isLoaded() const { return !items.empty(); }

}
