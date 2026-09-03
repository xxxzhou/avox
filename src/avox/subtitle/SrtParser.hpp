#pragma once

#include "avox/AvoxDef.h"
#include <string>
#include <vector>

namespace avox {

class SrtParser {
 public:
  struct RawItem {
    int64_t startMs = 0;
    int64_t endMs = 0;
    std::string text;
  };
  static bool parse(const char* content, size_t len,
                    std::vector<RawItem>& items);

 private:
  static int64_t parseTime(const char* time);
  static std::string trim(const char* str, size_t len);
};

}