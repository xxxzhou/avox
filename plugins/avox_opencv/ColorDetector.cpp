#include "ColorDetector.hpp"

#include "OpencvHelper.hpp"
#include "avox/AvoxVideo.h"

#include <algorithm>
#include <chrono>

namespace avox {

ColorDetector::ColorDetector() = default;
ColorDetector::~ColorDetector() = default;

// ============ 配置 ============
void ColorDetector::setColorSpace(ColorSpace cs_) { cs = cs_; }

void ColorDetector::setRange(int32_t c0Min, int32_t c1Min, int32_t c2Min,
                             int32_t c0Max, int32_t c1Max, int32_t c2Max) {
  lower = cv::Scalar(c0Min, c1Min, c2Min);
  upper = cv::Scalar(c0Max, c1Max, c2Max);
  hasRange = true;
}

void ColorDetector::setRoi(int32_t x, int32_t y, int32_t w, int32_t h) {
  roi = cv::Rect(x, y, w, h);
  useRoi = (w > 0 && h > 0);
}
void ColorDetector::clearRoi() { useRoi = false; }
void ColorDetector::setMinArea(int32_t area) { minArea = area > 0 ? area : 1; }
void ColorDetector::setMaxRegions(int32_t n) { maxRegions = n > 0 ? n : 1; }

// ============ 执行 ============
int32_t ColorDetector::detect(IImageBuffer* scene) {
  AVOX_CV_TRY;
  regions.clear();
  matchTimeMs = 0.0f;
  lastError.clear();
  if (!scene) {
    lastError = "detect: null scene";
    return 0;
  }
  if (!hasRange) {
    lastError = "detect: setRange not called";
    return 0;
  }
  auto start = std::chrono::steady_clock::now();
  cv::Mat bgr = imageBufferToBgr(scene);
  if (bgr.empty()) {
    lastError = "detect: unsupported scene format";
    return 0;
  }
  // 转到目标颜色空间
  cv::Mat work;
  switch (cs) {
    case ColorSpace::bgr: work = bgr; break;
    case ColorSpace::rgb: cv::cvtColor(bgr, work, cv::COLOR_BGR2RGB); break;
    case ColorSpace::hsv: cv::cvtColor(bgr, work, cv::COLOR_BGR2HSV); break;
    default:             work = bgr; break;
  }
  // ROI 裁剪
  cv::Mat searchArea = work;
  cv::Point roiOffset(0, 0);
  if (useRoi) {
    cv::Rect full(0, 0, work.cols, work.rows);
    cv::Rect r = roi & full;
    if (r.width <= 0 || r.height <= 0) {
      lastError = "detect: roi out of scene";
      return 0;
    }
    searchArea = work(r);
    roiOffset = r.tl();
  }
  // 颜色范围二值化 + 连通域
  cv::Mat mask;
  cv::inRange(searchArea, lower, upper, mask);
  cv::Mat labels, stats, centroids;
  int32_t n = cv::connectedComponentsWithStats(mask, labels, stats, centroids, 8);
  // label 0 = 背景, 从 1 起
  std::vector<ColorRegion> tmp;
  tmp.reserve(static_cast<size_t>(n > 0 ? n - 1 : 0));
  for (int32_t i = 1; i < n; ++i) {
    const int* st = stats.ptr<int>(i);
    int32_t x = st[cv::CC_STAT_LEFT];
    int32_t y = st[cv::CC_STAT_TOP];
    int32_t w = st[cv::CC_STAT_WIDTH];
    int32_t h = st[cv::CC_STAT_HEIGHT];
    int32_t area = st[cv::CC_STAT_AREA];
    if (area < minArea) continue;
    if (w <= 0 || h <= 0) continue;
    ColorRegion r;
    r.x = x + roiOffset.x;
    r.y = y + roiOffset.y;
    r.w = w;
    r.h = h;
    r.area = area;
    r.score = static_cast<double>(area) / static_cast<double>(w) / static_cast<double>(h);
    tmp.push_back(r);
  }
  // 面积降序, 截 maxRegions
  std::sort(tmp.begin(), tmp.end(),
            [](const ColorRegion& a, const ColorRegion& b) { return a.area > b.area; });
  if (static_cast<int32_t>(tmp.size()) > maxRegions) {
    tmp.resize(static_cast<size_t>(maxRegions));
  }
  regions = std::move(tmp);
  matchTimeMs = std::chrono::duration<float, std::milli>(
                    std::chrono::steady_clock::now() - start)
                    .count();
  return static_cast<int32_t>(regions.size());
  AVOX_CV_CATCH_RET(0);
}

// ============ 结果 ============
int32_t ColorDetector::getRegionCount() {
  return static_cast<int32_t>(regions.size());
}

bool ColorDetector::getRegion(int32_t index, ColorRegion* out) {
  int32_t idx;
  if (!out) return false;
  if (!resolveIndex(static_cast<int32_t>(regions.size()), index, idx)) return false;
  *out = regions[idx];
  return true;
}

float ColorDetector::getMatchTimeMs() { return matchTimeMs; }

const char* ColorDetector::getLastError() { return lastError.c_str(); }

// ============ 辅助 ============
bool ColorDetector::resolveIndex(int32_t count, int32_t index, int32_t& out) {
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
