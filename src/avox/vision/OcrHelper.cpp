#include "OcrHelper.hpp"

#include <algorithm>
#include <cstring>
#include <memory>

namespace avox {

std::vector<OcrItem> recognizeAll(IImageBuffer* buffer, double threshold) {
  if (!buffer) return {};
  std::unique_ptr<ITextRecognizer> rec(createTextRecognizer());
  if (!rec) return {};
  rec->setThreshold(threshold);
  int32_t n = rec->recognize(buffer);
  std::vector<OcrItem> items;
  items.reserve(n);
  for (int32_t k = 0; k < n; k++) {
    OcrItem item;
    OcrResult r{};
    const char* text = rec->getMatch(k, &r);
    if (!text) continue;
    item.x = r.x;
    item.y = r.y;
    item.w = r.w;
    item.h = r.h;
    item.score = r.score;
    item.text = text;
    items.push_back(std::move(item));
  }
  // 按阅读顺序: y 升序, 同 y 按 x 升序
  std::sort(items.begin(), items.end(), [](const OcrItem& a, const OcrItem& b) {
    if (a.y != b.y) return a.y < b.y;
    return a.x < b.x;
  });
  return items;
}

bool locateText(IImageBuffer* buffer, const char* text, double threshold,
                LocateResult* out) {
  if (out) {
    out->found = false;
    out->score = 0;
    out->imgCenter = vec2i(0, 0);
  }
  if (!buffer || !text) return false;
  std::unique_ptr<ITextRecognizer> rec(createTextRecognizer());
  if (!rec) return false;
  rec->setThreshold(threshold);
  int32_t n = rec->recognize(buffer);
  for (int32_t k = 0; k < n; k++) {
    OcrResult r{};
    const char* ocrText = rec->getMatch(k, &r);
    if (ocrText && std::strstr(ocrText, text)) {
      if (out) {
        out->found = true;
        out->score = r.score;
        out->imgCenter = getOcrCenter(r);
        out->text = ocrText;
      }
      return true;
    }
  }
  return false;
}

std::string formatOcrResult(const std::vector<OcrItem>& items) {
  std::string result = "[OCR识别结果]\n";
  if (items.empty()) {
    result += "无识别内容";
    return result;
  }
  for (const auto& item : items) {
    // 文字 (x=10,y=20,w=60,h=30, score=0.95)
    char line[512];
    snprintf(line, sizeof(line), "%s (x=%d,y=%d,w=%d,h=%d, score=%.2f)",
             item.text.c_str(), (int)item.x, (int)item.y,
             (int)item.w, (int)item.h, item.score);
    result += line;
    result += '\n';
  }
  char tail[64];
  snprintf(tail, sizeof(tail), "[共%d条]", (int)items.size());
  result += tail;
  return result;
}

}
