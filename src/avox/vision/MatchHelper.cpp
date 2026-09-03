#include "MatchHelper.hpp"

#include <memory>

#include "avox/module/AvoxManager.hpp"  // templateMatcherHub

namespace avox {

std::vector<MatchResult> matchAll(IImageBuffer* scene, IImageBuffer* tmpl,
                                double threshold) {
  if (!scene || !tmpl) return {};
  std::unique_ptr<ITemplateMatcher> matcher(
      AvoxManager::Get().templateMatcherHub.create("opencv"));
  if (!matcher) return {};
  if (matcher->addTemplate(tmpl, threshold) < 0) return {};
  int32_t n = matcher->match(scene);
  std::vector<MatchResult> items;
  items.reserve(n);
  for (int32_t k = 0; k < n; k++) {
    MatchResult r{};
    if (!matcher->getMatch(k, &r)) continue;
    items.push_back(r);
  }
  return items;
}

bool locateImage(IImageBuffer* buffer, IImageBuffer* tBuffer, double threshold,
                 LocateResult* out) {
  if (out) {
    out->found = false;
    out->score = 0;
    out->imgCenter = vec2i(0, 0);
  }
  if (!buffer || !tBuffer) return false;
  std::unique_ptr<ITemplateMatcher> matcher(
      AvoxManager::Get().templateMatcherHub.create("opencv"));
  if (!matcher) return false;
  if (matcher->addTemplate(tBuffer, threshold) < 0) return false;
  int32_t n = matcher->match(buffer);
  if (n <= 0) return false;
  MatchResult r{};
  if (!matcher->getMatch(0, &r)) return false;
  if (out) {
    out->found = true;
    out->score = r.score;
    out->imgCenter = getMatchCenter(r);
  }
  return true;
}

std::string formatMatchResult(const std::vector<MatchResult>& items) {
  std::string result = "[模板匹配结果]\n";
  if (items.empty()) {
    result += "无匹配";
    return result;
  }
  for (const auto& item : items) {
    char line[256];
    snprintf(line, sizeof(line), "(x=%d,y=%d,w=%d,h=%d, score=%.2f, tpl=%d)",
             (int)item.x, (int)item.y, (int)item.w, (int)item.h,
             item.score, (int)item.templateIndex);
    result += line;
    result += '\n';
  }
  char tail[64];
  snprintf(tail, sizeof(tail), "[共%d条]", (int)items.size());
  result += tail;
  return result;
}

}
