#include "SubtitleFile.hpp"

#include <cstring>
#include <fstream>

namespace avox {

SubtitleFile::SubtitleFile() { }

SubtitleFile::~SubtitleFile() { }

bool SubtitleFile::loadFile(const char* path) {
  if (!path) return false;

  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file.is_open()) return false;

  size_t size = file.tellg();
  if (size == 0) return false;

  file.seekg(0, std::ios::beg);
  std::string content;
  content.resize(size);
  file.read(&content[0], size);
  file.close();
  // Parse SRT
  std::vector<SrtParser::RawItem> rawItems;
  SrtParser::parse(content.c_str(), content.size(), rawItems);
  // Convert to stored format
  items.clear();
  for (const auto& raw : rawItems) {
    SubtitleItem item;
    item.startMs = raw.startMs;
    item.endMs = raw.endMs;
    item.text = raw.text;
    item.original = raw.text;
    item.language = Language::none;
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
}

bool SubtitleFile::isLoaded() const { return !items.empty(); }

}
