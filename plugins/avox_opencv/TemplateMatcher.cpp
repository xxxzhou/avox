#include "TemplateMatcher.hpp"

#include "OpencvHelper.hpp"
#include "avox/AvoxVideo.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace avox {

namespace {
// 接口 enum -> OpenCV method 值 (接口编号独立于 OpenCV, 实现内做映射)
int toCvMethod(TemplateMatchMethod m) {
  switch (m) {
    case TemplateMatchMethod::none:         return cv::TM_CCOEFF_NORMED;  // 未设置 -> 默认
    case TemplateMatchMethod::sqdiffNormed: return cv::TM_SQDIFF_NORMED;
    case TemplateMatchMethod::ccorrNormed:  return cv::TM_CCORR_NORMED;
    case TemplateMatchMethod::ccoeffNormed: return cv::TM_CCOEFF_NORMED;
  }
  return cv::TM_CCOEFF_NORMED;
}
}  // namespace

TemplateMatcher::TemplateMatcher() = default;
TemplateMatcher::~TemplateMatcher() = default;

// ============ 配置 ============
void TemplateMatcher::setMethod(TemplateMatchMethod method_) { method = method_; }
void TemplateMatcher::setOrderBy(MatchOrderBy orderBy_) { orderBy = orderBy_; }
void TemplateMatcher::setGreenMask(bool enable) { greenMask = enable; }
void TemplateMatcher::setGrayscale(bool enable) { grayscale = enable; }
void TemplateMatcher::setNmsIoU(float iou) { nmsIoU = iou; }
void TemplateMatcher::setRoi(int32_t x, int32_t y, int32_t w, int32_t h) {
  roi = cv::Rect(x, y, w, h);
  useRoi = (w > 0 && h > 0);
}
void TemplateMatcher::clearRoi() { useRoi = false; }

// ============ 模板 ============
int32_t TemplateMatcher::addTemplate(IImageBuffer* tmpl, double threshold) {
  if (!tmpl) {
    lastError = "addTemplate: null buffer";
    return -1;
  }
  cv::Mat bgr = imageBufferToBgr(tmpl);
  if (bgr.empty()) {
    lastError = "addTemplate: unsupported image format";
    return -1;
  }
  Template t;
  if (grayscale) {
    cv::Mat gray;
    cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
    t.mat = gray;
  } else {
    t.mat = bgr;
  }
  t.threshold = threshold;
  templates.push_back(std::move(t));
  return static_cast<int32_t>(templates.size() - 1);
}

int32_t TemplateMatcher::addTemplatePath(const char* path, double threshold) {
  if (!path || !path[0]) {
    lastError = "addTemplatePath: empty path";
    return -1;
  }
  IImageBuffer* buf = createImageBuffer();
  if (!buf) {
    lastError = "addTemplatePath: createImageBuffer failed";
    return -1;
  }
  bool ok = loadImagePath(path, buf);
  if (!ok) {
    lastError = "addTemplatePath: loadImagePath failed";
    delete buf;
    return -1;
  }
  int32_t idx = addTemplate(buf, threshold);
  delete buf;
  return idx;
}

void TemplateMatcher::clearTemplates() { templates.clear(); }

// ============ 执行 ============
int32_t TemplateMatcher::match(IImageBuffer* scene) {
  // ⚠ 2026-08-15 实机 P0 装甲：坏帧/边界尺寸会让 cv::matchTemplate 抛 cv::Exception，
  // 异常穿过绑定层直接 std::terminate 宿主 Python 进程（无法 catch）。
  // 任何 cv 异常 → lastError 归因，返回 0（无匹配），不崩进程。
  try {
    return matchImpl(scene);
  } catch (const cv::Exception& e) {
    lastError = std::string("match: cv exception: ") + e.what();
    results.clear();
    return 0;
  } catch (const std::exception& e) {
    lastError = std::string("match: exception: ") + e.what();
    results.clear();
    return 0;
  } catch (...) {
    lastError = "match: unknown exception";
    results.clear();
    return 0;
  }
}

int32_t TemplateMatcher::matchImpl(IImageBuffer* scene) {
  results.clear();
  matchTimeMs = 0.0f;
  lastError.clear();
  if (!scene) {
    lastError = "match: null scene";
    return 0;
  }
  if (templates.empty()) {
    lastError = "match: no template added";
    return 0;
  }
  auto start = std::chrono::steady_clock::now();
  cv::Mat sceneBgr = imageBufferToBgr(scene);
  if (sceneBgr.empty()) {
    lastError = "match: unsupported scene format";
    return 0;
  }
  // 灰度模式: scene 转单通道
  cv::Mat sceneMat;
  if (grayscale) {
    cv::cvtColor(sceneBgr, sceneMat, cv::COLOR_BGR2GRAY);
  } else {
    sceneMat = sceneBgr;
  }
  // ROI 裁剪
  cv::Mat searchArea = sceneMat;
  cv::Point roiOffset(0, 0);
  if (useRoi) {
    cv::Rect full(0, 0, sceneBgr.cols, sceneBgr.rows);
    cv::Rect r = roi & full;
    if (r.width <= 0 || r.height <= 0) {
      lastError = "match: roi out of scene";
      return 0;
    }
    // ⚠ 2026-08-15 修：原误用 sceneBgr(r)（彩色），grayscale 模式下模板是单通道
    // → matchTemplate type 不匹配断言。统一用 sceneMat（grayscale 时已转灰度）。
    searchArea = sceneMat(r);
    roiOffset = r.tl();
  }
  // 逐模板匹配, 合并候选
  std::vector<Result> all;
  for (int32_t ti = 0; ti < static_cast<int32_t>(templates.size()); ++ti) {
    const auto& tmpl = templates[ti];
    if (tmpl.mat.cols > searchArea.cols || tmpl.mat.rows > searchArea.rows) {
      lastError = "match: template larger than search area, skipped";
      continue;
    }
    // 掩码 (green_mask)
    cv::Mat mask;
    if (greenMask && !grayscale) {
      mask = createMask(tmpl.mat);
    }
    if (greenMask && grayscale) {
      lastError = "grayscale: greenMask ignored in grayscale mode";
    }
    // mask 仅 sqdiffNormed/ccorrNormed 支持; ccoeffNormed 自动降级为 ccorrNormed
    TemplateMatchMethod effective = (method == TemplateMatchMethod::none)
                                        ? TemplateMatchMethod::ccoeffNormed
                                        : method;
    if (!mask.empty() && effective == TemplateMatchMethod::ccoeffNormed) {
      effective = TemplateMatchMethod::ccorrNormed;
      lastError = "green_mask: ccoeffNormed unsupported with mask, downgraded to ccorrNormed";
    }
    int cvMethod = toCvMethod(effective);
    cv::Mat matched;
    if (mask.empty()) {
      cv::matchTemplate(searchArea, tmpl.mat, matched, cvMethod);
    } else {
      cv::matchTemplate(searchArea, tmpl.mat, matched, cvMethod, mask);
    }
    // SQDIFF 方向统一为越大越好 (用 enum 判断, 不依赖 cv 内部值)
    if (effective == TemplateMatchMethod::sqdiffNormed) {
      matched = cv::Scalar(1.0) - matched;
    }
    collectCandidates(matched, tmpl.mat.size(), roiOffset, ti, tmpl.threshold, all);
  }
  // NMS 去重 (跨模板合并) + 阈值过滤 + 排序
  nms(all);
  results.clear();
  results.reserve(all.size());
  for (const auto& r : all) {
    if (r.score >= r.threshold) results.push_back(r);
  }
  sortResults(results);
  matchTimeMs = std::chrono::duration<float, std::milli>(
                    std::chrono::steady_clock::now() - start)
                    .count();
  return static_cast<int32_t>(results.size());
}

// ============ 结果 ============
int32_t TemplateMatcher::getMatchCount() {
  return static_cast<int32_t>(results.size());
}

bool TemplateMatcher::getMatch(int32_t index, MatchResult* out) {
  int32_t idx;
  if (!out) return false;
  if (!resolveIndex(static_cast<int32_t>(results.size()), index, idx)) return false;
  const Result& r = results[idx];
  out->x = r.x;
  out->y = r.y;
  out->w = r.w;
  out->h = r.h;
  out->templateIndex = r.templateIndex;
  out->score = r.score;
  return true;
}

float TemplateMatcher::getMatchTimeMs() { return matchTimeMs; }

const char* TemplateMatcher::getLastError() { return lastError.c_str(); }

// ============ 辅助 ============
// toBgrMat 已提到 OpencvHelper::imageBufferToBgr (与 FeatureMatcher/ColorDetector 等共用)

cv::Mat TemplateMatcher::createMask(const cv::Mat& tmplBgr) const {
  cv::Mat mask;
  // 纯绿 BGR=(0,255,0)
  cv::inRange(tmplBgr, cv::Scalar(0, 255, 0), cv::Scalar(0, 255, 0), mask);
  cv::bitwise_not(mask, mask);  // 绿色=0(忽略), 其余=255(参与)
  int nz = cv::countNonZero(mask);
  if (nz >= mask.rows * mask.cols) return cv::Mat();  // 全图无绿, mask 无意义
  if (nz == 0) return cv::Mat();                      // 全图全绿, 无有效区域
  return mask;
}

void TemplateMatcher::collectCandidates(const cv::Mat& matched, cv::Size tmplSize,
                                        cv::Point roiOffset, int32_t templateIndex,
                                        double threshold, std::vector<Result>& out) {
  // 预过滤: 比阈值略低, 防 NMS 边缘丢失 (统一为越大越好坐标)
  double preFilter = threshold - 0.2;
  if (preFilter < 0.0) preFilter = 0.0;
  for (int row = 0; row < matched.rows; ++row) {
    const float* p = matched.ptr<float>(row);
    for (int col = 0; col < matched.cols; ++col) {
      float s = p[col];
      if (!std::isfinite(s)) continue;
      if (s < preFilter) continue;
      Result r;
      r.x = col + roiOffset.x;
      r.y = row + roiOffset.y;
      r.w = tmplSize.width;
      r.h = tmplSize.height;
      r.score = s;
      r.templateIndex = templateIndex;
      r.threshold = threshold;
      out.push_back(r);
    }
  }
}

void TemplateMatcher::nms(std::vector<Result>& results) const {
  if (nmsIoU <= 0.0f || results.size() <= 1) return;
  // 按 score 降序
  std::sort(results.begin(), results.end(),
            [](const Result& a, const Result& b) { return a.score > b.score; });
  std::vector<Result> kept;
  std::vector<char> suppressed(results.size(), 0);
  for (size_t i = 0; i < results.size(); ++i) {
    if (suppressed[i]) continue;
    kept.push_back(results[i]);
    cv::Rect bi(results[i].x, results[i].y, results[i].w, results[i].h);
    for (size_t j = i + 1; j < results.size(); ++j) {
      if (suppressed[j]) continue;
      cv::Rect bj(results[j].x, results[j].y, results[j].w, results[j].h);
      cv::Rect inter = bi & bj;
      int64_t bjArea = static_cast<int64_t>(results[j].w) * results[j].h;
      if (bjArea > 0 && static_cast<float>(inter.area()) >= nmsIoU * static_cast<float>(bjArea)) {
        suppressed[j] = 1;
      }
    }
  }
  results.swap(kept);
}

void TemplateMatcher::sortResults(std::vector<Result>& results) const {
  switch (orderBy) {
    case MatchOrderBy::horizontal:
      std::sort(results.begin(), results.end(), [](const Result& a, const Result& b) {
        return a.x != b.x ? a.x < b.x : a.y < b.y;
      });
      break;
    case MatchOrderBy::vertical:
      std::sort(results.begin(), results.end(), [](const Result& a, const Result& b) {
        return a.y != b.y ? a.y < b.y : a.x < b.x;
      });
      break;
    case MatchOrderBy::score:
      std::sort(results.begin(), results.end(),
                [](const Result& a, const Result& b) { return a.score > b.score; });
      break;
    case MatchOrderBy::area:
      std::sort(results.begin(), results.end(), [](const Result& a, const Result& b) {
        int64_t aa = static_cast<int64_t>(a.w) * a.h;
        int64_t ba = static_cast<int64_t>(b.w) * b.h;
        return aa != ba ? aa > ba : a.score > b.score;
      });
      break;
    case MatchOrderBy::none:
    default:
      break;
  }
}

bool TemplateMatcher::resolveIndex(int32_t count, int32_t index, int32_t& out) {
  if (count <= 0) return false;
  if (index >= 0 && index < count) {
    out = index;
    return true;
  }
  if (index < 0 && -index <= count) {
    out = count + index;
    return true;
  }
  return false;
}

}
