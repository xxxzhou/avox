#pragma once

#include "Subtitle.hpp"
#include "SrtParser.hpp"
#include "../module/RunTask.hpp"
#include <vector>
#include <memory>
#include <atomic>


namespace avox {

class SubtitleFile {
 public:
  SubtitleFile();
  ~SubtitleFile();

 private:
  std::vector<SubtitleItem> items;
  int32_t currentIndex = -1;
  bool translationEnabled = false;

 public:
  bool loadFile(const char* path);
  const SubtitleItem* getCurrent(int64_t ptsMs);
  void clear();
  bool isLoaded() const;
};

}