#include "MapMatcher.hpp"

#include "OpencvHelper.hpp"
#include "avox/AvoxVideo.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace avox {

MapMatcher::MapMatcher() = default;
MapMatcher::~MapMatcher() = default;

// ============ 配置 ============

void MapMatcher::setMapImage(IImageBuffer* mapImg) {
  if (!mapImg) {
    lastError = "setMapImage: null buffer";
    return;
  }
  coarseMap = imageBufferToBgr(mapImg);
  if (coarseMap.empty()) {
    lastError = "setMapImage: unsupported format";
    return;
  }
  // 预计算各通道 + 各通道平方 (对照 BGI FastSqDiffMatcher 构造函数)
  cv::Mat mapF;
  coarseMap.convertTo(mapF, CV_32F);
  cv::split(coarseMap, coarseMapChannels);
  cv::multiply(mapF, mapF, mapF);
  cv::split(mapF, coarseMapChannelsSq);
  // 计算粗→精坐标缩放 (如果 fineMap 已加载)
  if (!fineMap.empty()) {
    coarseToFineScaleX = static_cast<double>(fineMap.cols) / coarseMap.cols;
    coarseToFineScaleY = static_cast<double>(fineMap.rows) / coarseMap.rows;
  }
}

void MapMatcher::setMapImageByPath(const char* path) {
  if (!path || !path[0]) {
    lastError = "setMapImageByPath: empty path";
    return;
  }
  IImageBuffer* buf = createImageBuffer();
  if (!buf) {
    lastError = "setMapImageByPath: createImageBuffer failed";
    return;
  }
  bool ok = loadImagePath(path, buf);
  if (!ok) {
    lastError = "setMapImageByPath: loadImagePath failed";
    delete buf;
    return;
  }
  setMapImage(buf);
  delete buf;
}

void MapMatcher::setFineMapImage(IImageBuffer* mapImg) {
  if (!mapImg) {
    lastError = "setFineMapImage: null buffer";
    return;
  }
  cv::Mat bgr = imageBufferToBgr(mapImg);
  if (bgr.empty()) {
    lastError = "setFineMapImage: unsupported format";
    return;
  }
  cv::cvtColor(bgr, fineMap, cv::COLOR_BGR2GRAY);
  // 计算粗→精坐标缩放
  if (!coarseMap.empty()) {
    coarseToFineScaleX = static_cast<double>(fineMap.cols) / coarseMap.cols;
    coarseToFineScaleY = static_cast<double>(fineMap.rows) / coarseMap.rows;
  }
}

void MapMatcher::setFineMapImageByPath(const char* path) {
  if (!path || !path[0]) {
    lastError = "setFineMapImageByPath: empty path";
    return;
  }
  IImageBuffer* buf = createImageBuffer();
  if (!buf) {
    lastError = "setFineMapImageByPath: createImageBuffer failed";
    return;
  }
  bool ok = loadImagePath(path, buf);
  if (!ok) {
    lastError = "setFineMapImageByPath: loadImagePath failed";
    delete buf;
    return;
  }
  setFineMapImage(buf);
  delete buf;
}

void MapMatcher::setRoi(int32_t x, int32_t y, int32_t w, int32_t h) {
  roi = cv::Rect(x, y, w, h);
  useRoi = (w > 0 && h > 0);
}
void MapMatcher::clearRoi() { useRoi = false; }

void MapMatcher::setMask(IImageBuffer* mask) {
  // 二值掩码 (上层生成, 如 IMaskBuilder); 单通道 255=参与/0=排除
  if (!mask) {
    lastError = "setMask: null buffer";
    return;
  }
  cv::Mat bgr = imageBufferToBgr(mask);
  if (bgr.empty()) {
    lastError = "setMask: unsupported format";
    return;
  }
  cv::Mat gray;
  cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
  // 归一到 0/255 (容忍上层传任意灰度/二值)
  cv::threshold(gray, userMaskMat, 1, 255, cv::THRESH_BINARY);
  hasMask = true;
}
void MapMatcher::clearMask() {
  userMaskMat.release();
  hasMask = false;
}

void MapMatcher::setMinScore(double score) { minScore = score; }
void MapMatcher::setCoarseSize(int32_t size) { coarseSize = size; }
void MapMatcher::setExactSize(int32_t size) { exactSize = size; }
void MapMatcher::setSearchRadius(int32_t radius) { searchRadius = radius; }
void MapMatcher::setRoughSearchRadius(int32_t radius) { roughSearchRadius = radius; }
void MapMatcher::setSubPixel(bool enable) { useSubPixel = enable; }
void MapMatcher::setPrevPosition(double px, double py) {
  prevPos = cv::Point2f(static_cast<float>(px), static_cast<float>(py));
  hasPrevPos = true;
}
void MapMatcher::clearPrevPosition() { hasPrevPos = false; }

// ============ 执行 ============

int32_t MapMatcher::match(IImageBuffer* minimap) {
  // ⚠ 2026-08-15 实机 P0 装甲：坏帧/边界尺寸会让 cv::matchTemplate 抛 cv::Exception
  // （crossCorr 断言等），异常穿过绑定层直接 std::terminate 宿主 Python 进程。
  // 任何 cv 异常 → lastError 归因，返回 0（无匹配），不崩进程。
  try {
    return matchImpl(minimap);
  } catch (const cv::Exception& e) {
    lastError = std::string("match: cv exception: ") + e.what();
    result = MapMatchResult();
    return 0;
  } catch (const std::exception& e) {
    lastError = std::string("match: exception: ") + e.what();
    result = MapMatchResult();
    return 0;
  } catch (...) {
    lastError = "match: unknown exception";
    result = MapMatchResult();
    return 0;
  }
}

int32_t MapMatcher::matchImpl(IImageBuffer* minimap) {
  result = MapMatchResult();
  matchTimeMs = 0.0f;
  lastError.clear();
  useAutoRoi = false;

  if (!minimap) {
    lastError = "match: null minimap";
    return 0;
  }
  if (coarseMap.empty()) {
    lastError = "match: no map image set";
    return 0;
  }

  auto start = std::chrono::steady_clock::now();

  // 1. 小地图 → BGR (不做裁剪/朝向, 由上层预处理)
  cv::Mat miniBgr = imageBufferToBgr(minimap);
  if (miniBgr.empty()) {
    lastError = "match: unsupported minimap format";
    return 0;
  }

  // 2. 自动 ROI: prevPosition + roughSearchRadius → 限定粗匹配搜索区域
  if (hasPrevPos && roughSearchRadius > 0 && !useRoi) {
    int r = roughSearchRadius;
    int x0 = std::max(0, static_cast<int>(prevPos.x) - r);
    int y0 = std::max(0, static_cast<int>(prevPos.y) - r);
    int x1 = std::min(coarseMap.cols, static_cast<int>(prevPos.x) + r);
    int y1 = std::min(coarseMap.rows, static_cast<int>(prevPos.y) + r);
    if (x1 > x0 && y1 > y0) {
      autoRoi = cv::Rect(x0, y0, x1 - x0, y1 - y0);
      useAutoRoi = true;
    }
  }

  // 3. 粗匹配
  cv::Point2f roughPos(0, 0);
  double roughVal = 0;
  if (!roughMatch(miniBgr, roughPos, roughVal)) {
    lastError = "match: rough match failed";
    matchTimeMs = std::chrono::duration<float, std::milli>(
                      std::chrono::steady_clock::now() - start)
                      .count();
    return 0;
  }

  // 4. 精匹配 + 亚像素 (无 fineMap 时用粗匹配结果)
  cv::Point2f exactPos = roughPos;
  double exactVal = roughVal;
  if (!fineMap.empty()) {
    exactMatch(miniBgr, roughPos, exactPos, exactVal);
  }

  // 5. 置信度
  double score = normalizeScore(exactVal);
  matchTimeMs = std::chrono::duration<float, std::milli>(
                    std::chrono::steady_clock::now() - start)
                    .count();

  result.px = exactPos.x;
  result.py = exactPos.y;
  result.score = score;
  result.layerIndex = 0;

  if (score < minScore) {
    lastError = "match: score below threshold";
    return 0;
  }
  return 1;
}

// ============ 结果 ============

bool MapMatcher::getResult(MapMatchResult* out) {
  if (!out) return false;
  *out = result;
  return true;
}
float MapMatcher::getMatchTimeMs() { return matchTimeMs; }
const char* MapMatcher::getLastError() { return lastError.c_str(); }

// ============ 粗匹配 (对照 BGI FastSqDiffMatcher) ============

bool MapMatcher::roughMatch(const cv::Mat& bgrMinimap, cv::Point2f& outPos, double& outVal) {
  if (coarseMap.empty()) return false;

  // 缩放小地图到粗匹配尺寸
  cv::Mat miniResized;
  cv::resize(bgrMinimap, miniResized, cv::Size(coarseSize, coarseSize), 0, 0, cv::INTER_AREA);

  // 缩放用户掩码
  cv::Mat maskResized;
  if (hasMask && !userMaskMat.empty()) {
    cv::resize(userMaskMat, maskResized, cv::Size(coarseSize, coarseSize), 0, 0, cv::INTER_NEAREST);
  }

  // 掩膜应用: 先清零再 copyTo。
  // 注意: bitwise_and(src,src,dst,mask) 的 mask 只改写 mask!=0 处, 其余保持 dst 原值;
  //       若 dst 是新分配 Mat, 未掩膜区域是未初始化内存 (垃圾值), 会污染后续 matchTemplate。
  cv::Mat maskedMini = cv::Mat::zeros(miniResized.size(), miniResized.type());
  if (!maskResized.empty()) {
    miniResized.copyTo(maskedMini, maskResized);
  } else {
    maskedMini = miniResized.clone();
  }

  // 遮罩浮点版 {0,1}: 作为 SQDIFF 中 Σ(s²·m) 项的乘子
  cv::Mat maskF;
  if (!maskResized.empty()) {
    maskResized.convertTo(maskF, CV_32F);
    cv::normalize(maskF, maskF, 0, 1, cv::NORM_MINMAX);  // 0/255 → 0/1
  } else {
    maskF = cv::Mat::ones(coarseSize, coarseSize, CV_32F);
  }

  // 搜索区域
  cv::Point roiOffset(0, 0);
  cv::Rect searchRoi(0, 0, coarseMap.cols, coarseMap.rows);
  if (useRoi) {
    // 显式 setRoi 优先
    cv::Rect full(0, 0, coarseMap.cols, coarseMap.rows);
    cv::Rect r = roi & full;
    if (r.width < coarseSize || r.height < coarseSize) {
      lastError = "roughMatch: roi too small";
      return false;
    }
    searchRoi = r;
    roiOffset = r.tl();
  } else if (useAutoRoi) {
    // 自动 ROI (prevPosition + roughSearchRadius)
    cv::Rect full(0, 0, coarseMap.cols, coarseMap.rows);
    cv::Rect r = autoRoi & full;
    if (r.width < coarseSize || r.height < coarseSize) {
      // 自动 ROI 太小, 回退全图搜索
      useAutoRoi = false;
    } else {
      searchRoi = r;
      roiOffset = r.tl();
    }
  }
  if (coarseSize > searchRoi.width || coarseSize > searchRoi.height) {
    lastError = "roughMatch: minimap larger than search area";
    return false;
  }

  // 搜索区各通道 + 各通道平方 (预分割, 对照 BGI FastSqDiffMatcher)
  std::vector<cv::Mat> searchChannels;
  for (auto& ch : coarseMapChannels) searchChannels.push_back(ch(searchRoi));
  std::vector<cv::Mat> searchChannelsSq;
  for (auto& ch : coarseMapChannelsSq) searchChannelsSq.push_back(ch(searchRoi));

  std::vector<cv::Mat> miniChannels;
  cv::split(maskedMini, miniChannels);
  if (miniChannels.size() != searchChannels.size()) {
    lastError = "roughMatch: minimap/map channel count mismatch";
    return false;
  }

  // 结果图尺寸
  cv::Size resultSize(searchRoi.width - coarseSize + 1, searchRoi.height - coarseSize + 1);
  if (resultSize.width < 1 || resultSize.height < 1) {
    lastError = "roughMatch: invalid result size";
    return false;
  }

  // Σ(s·t·m) 逐位置: template=maskedMini 已掩膜, mask=0 处 t=0 自动不贡献
  cv::Mat crossSum = cv::Mat::zeros(resultSize, CV_32F);
  for (size_t i = 0; i < miniChannels.size(); ++i) {
    // ⚠ 2026-08-15 实机 avox corr.rows 崩溃防护: matchTemplate 要求模板 ≤ 搜索区域
    // (cv::crossCorr 断言 corr.rows <= img.rows + templ.rows - 1 否则 terminate)。
    // 任一通道模板 > 搜索图 → 跳过该通道匹配 (返回零贡献), 不崩。
    if (miniChannels[i].rows > searchChannels[i].rows ||
        miniChannels[i].cols > searchChannels[i].cols) {
      continue;
    }
    cv::Mat tmp;
    cv::matchTemplate(searchChannels[i], miniChannels[i], tmp, cv::TM_CCORR);
    crossSum += tmp;
  }

  // Σ(s²·m) 逐位置: maskF 作模板, 在搜索图各通道平方上 CCORR = 加权求和
  cv::Mat sqSourceSum = cv::Mat::zeros(resultSize, CV_32F);
  for (size_t i = 0; i < searchChannelsSq.size(); ++i) {
    if (maskF.rows > searchChannelsSq[i].rows ||
        maskF.cols > searchChannelsSq[i].cols) {
      continue;
    }
    cv::Mat tmp;
    cv::matchTemplate(searchChannelsSq[i], maskF, tmp, cv::TM_CCORR);
    sqSourceSum += tmp;
  }

  // Σ(t²·m) 标量 (常数, 模板固定; maskedMini 已掩膜故 mask 外 t=0)
  double sqTmplSum = 0.0;
  for (size_t i = 0; i < miniChannels.size(); ++i) {
    cv::Mat t2;
    cv::multiply(miniChannels[i], miniChannels[i], t2, 1.0, CV_32F);  // 显式 float 防 8U 平方溢出
    sqTmplSum += cv::sum(t2)[0];
  }

  // SQDIFF_NORMED = (Σs²·m + Σt²·m − 2·Σs·t·m) / sqrt(Σs²·m · Σt²·m), 范围 [0,1], 0=完美
  cv::Mat sqdiff;
  cv::scaleAdd(crossSum, -2.0, sqSourceSum, sqdiff);  // sqdiff = sqSourceSum − 2·crossSum
  sqdiff += sqTmplSum;

  cv::Mat product = sqSourceSum * sqTmplSum;
  product.setTo(0.0, product < 0.0);  // 钳负 (浮点误差致全黑区略负 → sqrt(负)=NaN)
  cv::Mat denom;
  cv::sqrt(product, denom);
  denom += 1e-6;  // 防除零
  cv::Mat sqdiffNorm;
  cv::divide(sqdiff, denom, sqdiffNorm);
  sqdiffNorm.setTo(0.0, sqdiffNorm < 0.0);  // 钳负 (浮点误差)

  // 找最小值 (SQDIFF_NORMED: 越小越好)
  double minVal, maxVal;
  cv::Point minLoc, maxLoc;
  cv::minMaxLoc(sqdiffNorm, &minVal, &maxVal, &minLoc, &maxLoc);

  outPos = cv::Point2f(roiOffset.x + minLoc.x + coarseSize / 2.0f,
                       roiOffset.y + minLoc.y + coarseSize / 2.0f);
  outVal = minVal;
  return true;
}

// ============ 精匹配 + 亚像素 ============

bool MapMatcher::exactMatch(const cv::Mat& bgrMinimap, const cv::Point2f& roughPos,
                            cv::Point2f& outPos, double& outVal) {
  if (fineMap.empty()) return false;

  // 粗匹配位置 (coarseMap 坐标系) → fineMap 坐标系
  cv::Point2f finePos(roughPos.x * coarseToFineScaleX, roughPos.y * coarseToFineScaleY);

  // 缩放小地图到精匹配尺寸
  cv::Mat miniResized;
  cv::resize(bgrMinimap, miniResized, cv::Size(exactSize, exactSize), 0, 0, cv::INTER_AREA);
  cv::Mat miniGray;
  cv::cvtColor(miniResized, miniGray, cv::COLOR_BGR2GRAY);

  // 缩放用户掩码
  cv::Mat maskResized;
  if (hasMask && !userMaskMat.empty()) {
    cv::resize(userMaskMat, maskResized, cv::Size(exactSize, exactSize), 0, 0, cv::INTER_NEAREST);
  }

  // 搜索区域: 以粗匹配位置 finePos 为中心, 半径 searchRadius, 再加模板半尺寸
  // (保证窗内任一模板位置的中心都落在 [finePos-searchRadius, finePos+searchRadius])
  int halfSearch = searchRadius;
  int halfTpl = exactSize / 2;
  cv::Rect searchRect(
      static_cast<int>(finePos.x) - halfSearch - halfTpl,
      static_cast<int>(finePos.y) - halfSearch - halfTpl,
      halfSearch * 2 + exactSize,
      halfSearch * 2 + exactSize);
  searchRect = searchRect & cv::Rect(0, 0, fineMap.cols, fineMap.rows);
  if (searchRect.width < exactSize || searchRect.height < exactSize) {
    return false;
  }

  cv::Mat searchArea = fineMap(searchRect);
  // 钳边后实际尺寸 < exactSize (或 < miniGray)，matchTemplate 内部 crossCorr 断言
  // 崩溃 (terminate)。在调用前显式校验 searchArea.cols/rows >= miniGray.cols/rows，
  // 不满足直接 return false (不调 matchTemplate, 不崩)。
  if (searchArea.cols < miniGray.cols || searchArea.rows < miniGray.rows) {
    lastError = "exactMatch: searchArea smaller than miniGray";
    return false;
  }
  if (!maskResized.empty()) {
    if (maskResized.cols != miniGray.cols || maskResized.rows != miniGray.rows) {
      lastError = "exactMatch: mask size mismatch";
      return false;
    }
  }

  // matchTemplate (SQDIFF_NORMED)
  cv::Mat matchResult;
  if (!maskResized.empty()) {
    cv::matchTemplate(searchArea, miniGray, matchResult, cv::TM_SQDIFF_NORMED, maskResized);
  } else {
    cv::matchTemplate(searchArea, miniGray, matchResult, cv::TM_SQDIFF_NORMED);
  }

  double minVal;
  cv::Point minLoc;
  cv::minMaxLoc(matchResult, &minVal, nullptr, &minLoc, nullptr);

  // 亚像素拟合 (对照 BGI SubPixMatch)
  cv::Point2f loc = minLoc;
  if (useSubPixel) {
    loc = subPixelFit(matchResult, minLoc);
  }

  // fineMap 坐标 → coarseMap 坐标 (统一输出坐标系)
  outPos = cv::Point2f(
      (searchRect.x + loc.x + exactSize / 2.0f) / coarseToFineScaleX,
      (searchRect.y + loc.y + exactSize / 2.0f) / coarseToFineScaleY);
  outVal = minVal;
  return true;
}

// ============ 亚像素拟合 (对照 BGI SubPixMatch.Fit) ============

cv::Point2f MapMatcher::subPixelFit(const cv::Mat& resultImg, const cv::Point& loc) {
  // 3×3 邻域拟合二次曲面 z = c0·x² + c1·y² + c2·xy + c3·x + c4·y + c5, 求驻点 (亚像素偏移)
  // 6×9 系数矩阵 (照抄 SubPixMatch.cs): 9 像素行优先 (idx0=左上(-1,-1), idx4=中心(0,0), idx8=右下(1,1))
  static const double kFeatures[6][9] = {
      {1.0 / 6.0, -1.0 / 3.0, 1.0 / 6.0, 1.0 / 6.0, -1.0 / 3.0, 1.0 / 6.0, 1.0 / 6.0, -1.0 / 3.0, 1.0 / 6.0},
      {1.0 / 6.0, 1.0 / 6.0, 1.0 / 6.0, -1.0 / 3.0, -1.0 / 3.0, -1.0 / 3.0, 1.0 / 6.0, 1.0 / 6.0, 1.0 / 6.0},
      {1.0 / 4.0, 0.0, -1.0 / 4.0, 0.0, 0.0, 0.0, -1.0 / 4.0, 0.0, 1.0 / 4.0},
      {-1.0 / 6.0, 0.0, 1.0 / 6.0, -1.0 / 6.0, 0.0, 1.0 / 6.0, -1.0 / 6.0, 0.0, 1.0 / 6.0},
      {-1.0 / 6.0, -1.0 / 6.0, -1.0 / 6.0, 0.0, 0.0, 0.0, 1.0 / 6.0, 1.0 / 6.0, 1.0 / 6.0},
      {-1.0 / 9.0, 2.0 / 9.0, -1.0 / 9.0, 2.0 / 9.0, 5.0 / 9.0, 2.0 / 9.0, -1.0 / 9.0, 2.0 / 9.0, -1.0 / 9.0}};

  int W = resultImg.cols;
  int H = resultImg.rows;
  if (W < 3 || H < 3) return cv::Point2f(static_cast<float>(loc.x), static_cast<float>(loc.y));

  // clamp 到 [1, W-2]×[1, H-2] (保证 3×3 邻域不越界)
  int cx = std::max(1, std::min(loc.x, W - 2));
  int cy = std::max(1, std::min(loc.y, H - 2));

  // 取 3×3 邻域 (行优先: idx=(dy+1)*3+(dx+1))
  double pix[9];
  for (int dy = -1; dy <= 1; ++dy) {
    for (int dx = -1; dx <= 1; ++dx) {
      pix[(dy + 1) * 3 + (dx + 1)] = resultImg.at<float>(cy + dy, cx + dx);
    }
  }

  // 6 个系数: c[i] = Σ_j kFeatures[i][j]·pix[j]
  double c[6];
  for (int i = 0; i < 6; ++i) {
    double s = 0.0;
    for (int j = 0; j < 9; ++j) s += kFeatures[i][j] * pix[j];
    c[i] = s;
  }

  // 驻点: 解 ∂z/∂x = ∂z/∂y = 0 (判别式 disc = c2² − 4·c0·c1)
  double disc = c[2] * c[2] - 4.0 * c[0] * c[1];
  if (std::abs(disc) < 1e-20) {
    // 退化, 直接返回整数点
    return cv::Point2f(static_cast<float>(cx), static_cast<float>(cy));
  }
  double offX = (2.0 * c[1] * c[3] - c[2] * c[4]) / disc;
  double offY = (2.0 * c[0] * c[4] - c[2] * c[3]) / disc;
  // 偏移不允许跨出 ±1 像素 (极值必须落在中心像素内)
  offX = std::max(-1.0, std::min(1.0, offX));
  offY = std::max(-1.0, std::min(1.0, offY));
  return cv::Point2f(static_cast<float>(cx + offX), static_cast<float>(cy + offY));
}

// ============ 置信度 ============

double MapMatcher::normalizeScore(double sqDiffVal) {
  // SQDIFF_NORMED ([0,1], 0=完美匹配) → score = 1 - val, 钳到 [0,1]
  double score = 1.0 - sqDiffVal;
  if (std::isnan(score)) score = 0.0;
  if (score < 0.0) score = 0.0;
  if (score > 1.0) score = 1.0;
  return score;
}

// ============ 辅助 ============

cv::Mat MapMatcher::imageBufferToBgr(IImageBuffer* buf) {
  // 委派给 OpencvHelper 的自由函数 (avox 命名空间)
  return avox::imageBufferToBgr(buf);
}

}
