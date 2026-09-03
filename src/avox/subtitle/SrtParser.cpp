#include "SrtParser.hpp"

#include <cctype>

namespace avox {

int64_t SrtParser::parseTime(const char* time) {
  // Format: 00:00:00,000
  int64_t h = 0, m = 0, s = 0, ms = 0;
  sscanf(time, "%lld:%lld:%lld,%lld", &h, &m, &s, &ms);
  return (h * 3600 + m * 60 + s) * 1000 + ms;
}

std::string SrtParser::trim(const char* str, size_t len) {
  while (len > 0 && std::isspace(static_cast<unsigned char>(str[len - 1]))) {
    len--;
  }
  size_t start = 0;
  while (start < len && std::isspace(static_cast<unsigned char>(str[start]))) {
    start++;
  }
  return std::string(str + start, len - start);
}

bool SrtParser::parse(const char* content, size_t len,
                      std::vector<RawItem>& items) {
  if (!content || len == 0) return false;

  items.clear();
  std::string text;
  text.reserve(len);
  text.assign(content, len);

  size_t pos = 0;
  while (pos < text.size()) {
    size_t lineStart = pos;
    while (pos < text.size() && text[pos] != '\n') {
      pos++;
    }
    std::string line = text.substr(lineStart, pos - lineStart);
    if (pos < text.size() && text[pos] == '\n') pos++;

    // Skip empty lines
    bool allSpace = true;
    for (char c : line) {
      if (!std::isspace(static_cast<unsigned char>(c))) {
        allSpace = false;
        break;
      }
    }
    if (allSpace) continue;

    // Check if it's a number (subtitle index)
    bool isIndex = true;
    for (char c : line) {
      if (!std::isdigit(static_cast<unsigned char>(c))) {
        isIndex = false;
        break;
      }
    }

    if (isIndex) {
      // Read time line
      lineStart = pos;
      while (pos < text.size() && text[pos] != '\n') {
        pos++;
      }
      std::string timeLine = text.substr(lineStart, pos - lineStart);
      if (pos < text.size() && text[pos] == '\n') pos++;

      // Parse time: 00:00:00,000 --> 00:00:00,000
      size_t arrowPos = timeLine.find("-->");
      if (arrowPos == std::string::npos) continue;

      std::string startTimeStr = trim(timeLine.c_str(), arrowPos);
      std::string endTimeStr =
          trim(timeLine.c_str() + arrowPos + 3,
               timeLine.size() - arrowPos - 3);

      int64_t startMs = parseTime(startTimeStr.c_str());
      int64_t endMs = parseTime(endTimeStr.c_str());

      // Read text lines until empty line
      std::string subtitleText;
      while (pos < text.size()) {
        lineStart = pos;
        while (pos < text.size() && text[pos] != '\n') {
          pos++;
        }
        std::string textLine = text.substr(lineStart, pos - lineStart);
        if (pos < text.size() && text[pos] == '\n') pos++;

        // Check if empty line (end of subtitle)
        bool allSpace2 = true;
        for (char c : textLine) {
          if (!std::isspace(static_cast<unsigned char>(c))) {
            allSpace2 = false;
            break;
          }
        }
        if (allSpace2) break;

        if (!subtitleText.empty()) subtitleText += "\n";
        subtitleText += textLine;
      }

      if (!subtitleText.empty()) {
        RawItem item;
        item.startMs = startMs;
        item.endMs = endMs;
        item.text = subtitleText;
        items.push_back(item);
      }
    }
  }

  return !items.empty();
}

}