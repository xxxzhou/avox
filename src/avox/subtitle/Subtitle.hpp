#pragma once

#include "../AvoxDef.h"
#include "../audio/AudioStt.hpp"
#include <string>

namespace avox {

struct SubtitleItem {
  int64_t startMs = 0;
  int64_t endMs = 0;
  std::string text;
  std::string original;
  Language language = Language::none;
};

}