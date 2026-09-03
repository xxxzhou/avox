#include "FeatureMatcher.hpp"

#include "OpencvHelper.hpp"
#include "avox/AvoxVideo.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>

// BGI .kp.bin = N×28B cv::KeyPoint 原生小端布局 (C# OpenCvSharp Marshal 直拷 VectorOfKeyPoint);
// 编译期校验 C++ cv::KeyPoint 与该布局一致, 不符则 BGI 特征文件不可直接 ifstream。
static_assert(sizeof(cv::KeyPoint) == 28, "BGI .kp.bin assumes cv::KeyPoint == 28 bytes");

namespace avox {

FeatureMatcher::FeatureMatcher() = default;
FeatureMatcher::~FeatureMatcher() = default;

// ============ 配置 ============
void FeatureMatcher::setMethod(FeatureMethod method_) { method = method_; }
void FeatureMatcher::setMaxFeatures(int32_t n) { maxFeatures = n > 0 ? n : 1000; }
void FeatureMatcher::setRatioThreshold(float ratio) {
  ratioThreshold = (ratio > 0.0f && ratio < 1.0f) ? ratio : 0.75f;
}
void FeatureMatcher::setMinInliers(int32_t n) { minInliers = n > 0 ? n : 1; }
void FeatureMatcher::setRoi(int32_t x, int32_t y, int32_t w, int32_t h) {
  roi = cv::Rect(x, y, w, h);
  useRoi = (w > 0 && h > 0);
}
void FeatureMatcher::clearRoi() { useRoi = false; }

// ============ 检测器/参考图 ============
cv::Ptr<cv::Feature2D> FeatureMatcher::makeDetector() const {
  switch (method) {
    case FeatureMethod::orb:   return cv::ORB::create(maxFeatures);
    case FeatureMethod::akaze: return cv::AKAZE::create();
    case FeatureMethod::sift:
    default:                   return cv::SIFT::create(maxFeatures);
  }
}

bool FeatureMatcher::extractRef(IImageBuffer* buf, Ref& out) {
  cv::Mat bgr = imageBufferToBgr(buf);
  if (bgr.empty()) return false;
  auto det = makeDetector();
  det->detectAndCompute(bgr, cv::noArray(), out.kps, out.desc);
  out.mat = std::move(bgr);
  return !out.desc.empty() && !out.kps.empty();
}

int32_t FeatureMatcher::addReference(IImageBuffer* ref) {
  if (!ref) {
    lastError = "addReference: null buffer";
    return -1;
  }
  Ref r;
  if (!extractRef(ref, r)) {
    lastError = "addReference: no features detected (unsupported format or textureless?)";
    return -1;
  }
  refs.push_back(std::move(r));
  return static_cast<int32_t>(refs.size() - 1);
}

int32_t FeatureMatcher::addReferencePath(const char* path) {
  if (!path || !path[0]) {
    lastError = "addReferencePath: empty path";
    return -1;
  }
  IImageBuffer* buf = createImageBuffer();
  if (!buf) {
    lastError = "addReferencePath: createImageBuffer failed";
    return -1;
  }
  bool ok = loadImagePath(path, buf);
  if (!ok) {
    lastError = "addReferencePath: loadImagePath failed";
    delete buf;
    return -1;
  }
  int32_t idx = addReference(buf);
  delete buf;
  return idx;
}

void FeatureMatcher::clearReferences() { refs.clear(); }

// ============ 执行 ============
int32_t FeatureMatcher::match(IImageBuffer* scene) {
  AVOX_CV_TRY;
  results.clear();
  matchTimeMs = 0.0f;
  lastError.clear();
  if (!scene) {
    lastError = "match: null scene";
    return 0;
  }
  if (refs.empty()) {
    lastError = "match: no reference added";
    return 0;
  }
  auto start = std::chrono::steady_clock::now();
  cv::Mat sceneBgr = imageBufferToBgr(scene);
  if (sceneBgr.empty()) {
    lastError = "match: unsupported scene format";
    return 0;
  }
  // ROI 裁剪
  cv::Mat searchArea = sceneBgr;
  cv::Point roiOffset(0, 0);
  if (useRoi) {
    cv::Rect full(0, 0, sceneBgr.cols, sceneBgr.rows);
    cv::Rect r = roi & full;
    if (r.width <= 0 || r.height <= 0) {
      lastError = "match: roi out of scene";
      return 0;
    }
    searchArea = sceneBgr(r);
    roiOffset = r.tl();
  }
  // 场景特征
  auto det = makeDetector();
  std::vector<cv::KeyPoint> sceneKps;
  cv::Mat sceneDesc;
  det->detectAndCompute(searchArea, cv::noArray(), sceneKps, sceneDesc);
  if (sceneDesc.empty() || sceneKps.empty()) {
    lastError = "match: no scene features detected";
    return 0;
  }
  // 匹配器范数: ORB 二值描述子用 HAMMING, SIFT/AKAZE 浮点用 L2
  int normType = (method == FeatureMethod::orb) ? cv::NORM_HAMMING : cv::NORM_L2;
  cv::BFMatcher matcher(normType);

  for (int32_t ri = 0; ri < static_cast<int32_t>(refs.size()); ++ri) {
    const Ref& ref = refs[ri];
    if (ref.desc.empty()) continue;
    // knn 匹配 (query=ref, train=scene)
    std::vector<std::vector<cv::DMatch>> knn;
    matcher.knnMatch(ref.desc, sceneDesc, knn, 2);
    // Lowe 比率测试
    std::vector<cv::Point2f> refPts, scnPts;
    refPts.reserve(knn.size());
    scnPts.reserve(knn.size());
    for (const auto& m : knn) {
      if (m.size() < 2) continue;
      if (m[0].distance < ratioThreshold * m[1].distance) {
        refPts.push_back(ref.kps[m[0].queryIdx].pt);
        scnPts.push_back(sceneKps[m[0].trainIdx].pt);
      }
    }
    if (static_cast<int32_t>(refPts.size()) < 4) continue;  // 单应性至少需 4 点
    // RANSAC 单应性 (ref 平面 -> scene 平面)
    std::vector<uchar> mask;
    cv::Mat H = cv::findHomography(refPts, scnPts, cv::RANSAC, 3.0, mask);
    if (H.empty()) continue;
    int32_t inliers = 0;
    for (uchar c : mask)
      if (c) ++inliers;
    if (inliers < minInliers) continue;
    // 命中框 = 参考图四角经 H 投影的轴对齐外接矩形 (scene/searchArea 坐标系)
    std::vector<cv::Point2f> corners = {{0, 0},
                                        {static_cast<float>(ref.mat.cols), 0},
                                        {static_cast<float>(ref.mat.cols), static_cast<float>(ref.mat.rows)},
                                        {0, static_cast<float>(ref.mat.rows)}};
    std::vector<cv::Point2f> proj;
    cv::perspectiveTransform(corners, proj, H);
    float minX = proj[0].x, minY = proj[0].y, maxX = proj[0].x, maxY = proj[0].y;
    for (const auto& p : proj) {
      minX = std::min(minX, p.x);
      minY = std::min(minY, p.y);
      maxX = std::max(maxX, p.x);
      maxY = std::max(maxY, p.y);
    }
    FeatureMatchResult r;
    r.x = static_cast<int32_t>(std::floor(minX)) + roiOffset.x;
    r.y = static_cast<int32_t>(std::floor(minY)) + roiOffset.y;
    r.w = static_cast<int32_t>(std::ceil(maxX - minX));
    r.h = static_cast<int32_t>(std::ceil(maxY - minY));
    r.refIndex = ri;
    r.score = static_cast<double>(inliers) / static_cast<double>(refPts.size());
    results.push_back(r);
  }
  // score 降序
  std::sort(results.begin(), results.end(),
            [](const FeatureMatchResult& a, const FeatureMatchResult& b) { return a.score > b.score; });
  matchTimeMs = std::chrono::duration<float, std::milli>(
                    std::chrono::steady_clock::now() - start)
                    .count();
  return static_cast<int32_t>(results.size());
  AVOX_CV_CATCH_RET(0);
}

// ============ 结果 ============
int32_t FeatureMatcher::getMatchCount() {
  return static_cast<int32_t>(results.size());
}

bool FeatureMatcher::getMatch(int32_t index, FeatureMatchResult* out) {
  int32_t idx;
  if (!out) return false;
  if (!resolveIndex(static_cast<int32_t>(results.size()), index, idx)) return false;
  *out = results[idx];
  return true;
}

float FeatureMatcher::getMatchTimeMs() { return matchTimeMs; }

const char* FeatureMatcher::getLastError() { return lastError.c_str(); }

// ============ 辅助 ============
bool FeatureMatcher::resolveIndex(int32_t count, int32_t index, int32_t& out) {
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

// ============ BGI 大地图 SIFT 定位 (预存底图 + 切块局部 FLANN) ============
// 复刻 BetterGI BigMapTeyvat256Layer + Feature2DExtensions:
//   train(底图)特征离线预存 (.kp.bin/.mat.png) + 切块缓存; query(视口)实时 detectAndCompute。
//   局部 = FLANN knnMatch + Lowe 0.75 (BGI KnnMatch); 全图兜底 = FLANN match + 距离阈值 (BGI Match)。
//   输出 = query 中心经 H(query→train) 投影到 train 的坐标 (FeatureMatchResult.x/y, w=h=0 点哨兵)。
//   内嵌 BGI 常量, 强制 SIFT 无参, 不读 setMethod/setMaxFeatures 等配置 (保持定位基线纯净)。

bool FeatureMatcher::loadTrainFeaturesPath(const char* kpPath, const char* descPath,
                                           int32_t imgW, int32_t imgH,
                                           int32_t blockRows, int32_t blockCols) {
  return loadTrainFeaturesPathImpl(kpPath, descPath, imgW, imgH, blockRows, blockCols);
}

bool FeatureMatcher::loadTrainFeaturesPathImpl(const char* kpPath, const char* descPath,
                                               int32_t imgW, int32_t imgH,
                                               int32_t blockRows, int32_t blockCols) {
  train.valid = false;
  train.grid.clear();
  train.allKps.clear();
  train.allDesc.release();
  if (!kpPath || !kpPath[0] || !descPath || !descPath[0]) {
    lastError = "loadTrainFeaturesPath: empty path";
    return false;
  }
  if (imgW <= 0 || imgH <= 0 || blockRows <= 0 || blockCols <= 0) {
    lastError = "loadTrainFeaturesPath: invalid dims";
    return false;
  }
  // .kp.bin: N×28B cv::KeyPoint (C++ 原生布局, BGI OpenCvSharp Marshal 直拷) → ifstream
  std::ifstream kpFile(kpPath, std::ios::binary | std::ios::ate);
  if (!kpFile) {
    lastError = "loadTrainFeaturesPath: cannot open kp.bin";
    return false;
  }
  std::streamoff kpSize = kpFile.tellg();
  if (kpSize < 0 || static_cast<size_t>(kpSize) % sizeof(cv::KeyPoint) != 0) {
    lastError = "loadTrainFeaturesPath: kp.bin size not multiple of KeyPoint(28B)";
    return false;
  }
  size_t n = static_cast<size_t>(kpSize) / sizeof(cv::KeyPoint);
  std::vector<cv::KeyPoint> kps(n);
  if (n > 0) {
    kpFile.seekg(0, std::ios::beg);
    kpFile.read(reinterpret_cast<char*>(kps.data()), static_cast<std::streamsize>(kpSize));
    if (!kpFile) {
      lastError = "loadTrainFeaturesPath: kp.bin read failed";
      return false;
    }
  }
  // .mat.png: 128×N 灰度 → CV_32FC1 (BGI LoadDescMat: imread GRAYSCALE + convertTo, 无归一化)
  cv::Mat rawDesc = cv::imread(descPath, cv::IMREAD_GRAYSCALE);
  if (rawDesc.empty()) {
    lastError = "loadTrainFeaturesPath: cannot read desc png";
    return false;
  }
  if (rawDesc.cols != 128 || rawDesc.rows != static_cast<int>(n)) {
    lastError = "loadTrainFeaturesPath: desc rows!=kp count or cols!=128";
    return false;
  }
  cv::Mat desc;
  rawDesc.convertTo(desc, CV_32FC1);
  train.imgW = imgW;
  train.imgH = imgH;
  train.rows = blockRows;
  train.cols = blockCols;
  train.cellW = imgW / blockCols;   // 整数除法 (BGI SplitFeatures)
  train.cellH = imgH / blockRows;
  train.allKps = std::move(kps);
  train.allDesc = std::move(desc);
  splitTrainFeatures();
  train.valid = true;
  return true;
}

// BGI SplitFeatures: 按 cellW×cellH 把 allKps 落格; 坐标不偏移 (仍是 train 全图坐标)。
// 先收集各 cell 的全局 kp 下标, 再一次性抽描述子行 (对齐 BGI InitBlockMat, 避免逐行 push)。
void FeatureMatcher::splitTrainFeatures() {
  train.grid.assign(static_cast<size_t>(train.rows) * train.cols, TrainBlock{});
  const int32_t cellW = train.cellW, cellH = train.cellH;
  const int32_t cols = train.cols, rows = train.rows;
  for (size_t i = 0; i < train.allKps.size(); ++i) {
    const cv::KeyPoint& kp = train.allKps[i];
    int32_t cx = static_cast<int32_t>(kp.pt.x / cellW);
    int32_t cy = static_cast<int32_t>(kp.pt.y / cellH);
    if (cx < 0) cx = 0; else if (cx >= cols) cx = cols - 1;
    if (cy < 0) cy = 0; else if (cy >= rows) cy = rows - 1;
    train.grid[static_cast<size_t>(cy) * cols + cx].idx.push_back(static_cast<int>(i));
  }
  for (auto& blk : train.grid) {
    if (blk.idx.empty()) continue;
    blk.kps.reserve(blk.idx.size());
    blk.desc.create(static_cast<int>(blk.idx.size()), 128, CV_32FC1);
    for (size_t k = 0; k < blk.idx.size(); ++k) {
      int gi = blk.idx[k];
      blk.kps.push_back(train.allKps[gi]);
      train.allDesc.row(gi).copyTo(blk.desc.row(static_cast<int>(k)));
    }
    blk.idx.clear();
    blk.idx.shrink_to_fit();
  }
}

// BGI GetCellRange + KnnMatchLocal(±expandCells) + MergeFeaturesInRange: 取 roi 覆盖格 ±expandCells 合并。
// 坐标不偏移; 直接合并各 cell 已建的 desc (不需重建)。
bool FeatureMatcher::mergeTrainFeaturesInRange(const cv::Rect& roi, int32_t expandCells,
                                               std::vector<cv::KeyPoint>& outKps,
                                               cv::Mat& outDesc) const {
  outKps.clear();
  outDesc.release();
  if (!train.valid || train.cellW <= 0 || train.cellH <= 0) return false;
  auto clampR = [&](int32_t v) { return v < 0 ? 0 : (v >= train.rows ? train.rows - 1 : v); };
  auto clampC = [&](int32_t v) { return v < 0 ? 0 : (v >= train.cols ? train.cols - 1 : v); };
  // rect 覆盖格 (整数除法, 对齐 BGI GetCellRange)
  int32_t sr = clampR(roi.y / train.cellH);
  int32_t er = clampR((roi.y + roi.height) / train.cellH);
  int32_t sc = clampC(roi.x / train.cellW);
  int32_t ec = clampC((roi.x + roi.width) / train.cellW);
  if (er < sr) er = sr;
  if (ec < sc) ec = sc;
  // ±expandCells (BGI KnnMatchLocal), 再钳
  sr = clampR(sr - expandCells);
  er = clampR(er + expandCells);
  sc = clampC(sc - expandCells);
  ec = clampC(ec + expandCells);
  for (int32_t r = sr; r <= er; ++r) {
    for (int32_t c = sc; c <= ec; ++c) {
      const TrainBlock& blk = train.grid[static_cast<size_t>(r) * train.cols + c];
      if (blk.kps.empty()) continue;
      outKps.insert(outKps.end(), blk.kps.begin(), blk.kps.end());
      outDesc.push_back(blk.desc);   // 追加 N×128 行 (CV_32FC1)
    }
  }
  return !outKps.empty();
}

// BGI KnnMatch: FLANN knnMatch(query,train,2) + Lowe 0.75 → good≥7 → RANSAC(query→train,3.0)
// → perspectiveTransform(query 中心) 得 train 坐标。good<7 或 H 空返回 false (对齐 BGI return default)。
bool FeatureMatcher::knnLocate(const std::vector<cv::KeyPoint>& qKps, const cv::Mat& qDesc,
                               int32_t queryW, int32_t queryH,
                               const std::vector<cv::KeyPoint>& tKps, const cv::Mat& tDesc,
                               cv::Point2d& outTrainCenter, double& outScore) {
  outScore = 0.0;
  if (qDesc.empty() || tDesc.empty() || static_cast<int32_t>(qKps.size()) < 7) return false;
  std::vector<std::vector<cv::DMatch>> knn;
  try {
    cv::FlannBasedMatcher flann;
    flann.knnMatch(qDesc, tDesc, knn, 2);   // query=视口, train=底图
  } catch (const cv::Exception& e) {
    lastError = std::string("knnLocate FLANN: ") + e.what();
    return false;
  }
  std::vector<cv::Point2f> qPts, tPts;
  for (const auto& m : knn) {
    if (m.size() < 2) continue;
    if (m[0].distance < 0.75f * m[1].distance) {   // Lowe 比率
      qPts.push_back(qKps[m[0].queryIdx].pt);
      tPts.push_back(tKps[m[0].trainIdx].pt);
    }
  }
  if (static_cast<int32_t>(qPts.size()) < 7) return false;   // BGI goodMatches≥7
  std::vector<uchar> mask;
  cv::Mat H = cv::findHomography(qPts, tPts, cv::RANSAC, 3.0, mask);   // query→train
  if (H.empty()) return false;
  int32_t inliers = 0;
  for (uchar c : mask)
    if (c) ++inliers;
  std::vector<cv::Point2f> qc = {{queryW / 2.0f, queryH / 2.0f}};
  std::vector<cv::Point2f> tc;
  cv::perspectiveTransform(qc, tc, H);   // query 中心 → train 坐标
  outTrainCenter = cv::Point2d(tc[0].x, tc[0].y);
  outScore = static_cast<double>(inliers) / static_cast<double>(qPts.size());
  return true;
}

// BGI Match (兜底): FLANN match(单 NN) + 距离阈值 max(minDist×2,0.02) → RANSAC → query 中心投影。
bool FeatureMatcher::matchLocate(const std::vector<cv::KeyPoint>& qKps, const cv::Mat& qDesc,
                                 int32_t queryW, int32_t queryH,
                                 const std::vector<cv::KeyPoint>& tKps, const cv::Mat& tDesc,
                                 cv::Point2d& outTrainCenter, double& outScore) {
  outScore = 0.0;
  if (qDesc.empty() || tDesc.empty() || static_cast<int32_t>(qKps.size()) < 4) return false;
  std::vector<cv::DMatch> matches;
  try {
    cv::FlannBasedMatcher flann;
    flann.match(qDesc, tDesc, matches);   // 单最近邻, query=视口 train=底图
  } catch (const cv::Exception& e) {
    lastError = std::string("matchLocate FLANN: ") + e.what();
    return false;
  }
  if (matches.empty()) return false;
  double minDist = matches[0].distance;
  for (const auto& m : matches)
    if (m.distance < minDist) minDist = m.distance;
  double thresh = std::max(minDist * 2.0, 0.02);
  std::vector<cv::Point2f> qPts, tPts;
  for (const auto& m : matches) {
    if (m.distance < thresh) {
      qPts.push_back(qKps[m.queryIdx].pt);
      tPts.push_back(tKps[m.trainIdx].pt);
    }
  }
  if (static_cast<int32_t>(qPts.size()) < 4) return false;   // findHomography 最低 4 点
  std::vector<uchar> mask;
  cv::Mat H = cv::findHomography(qPts, tPts, cv::RANSAC, 3.0, mask);
  if (H.empty()) return false;
  int32_t inliers = 0;
  for (uchar c : mask)
    if (c) ++inliers;
  std::vector<cv::Point2f> qc = {{queryW / 2.0f, queryH / 2.0f}};
  std::vector<cv::Point2f> tc;
  cv::perspectiveTransform(qc, tc, H);
  outTrainCenter = cv::Point2d(tc[0].x, tc[0].y);
  outScore = inliers > 0 ? static_cast<double>(inliers) / static_cast<double>(qPts.size()) : 0.0;
  return true;
}

// query(视口) 局部定位: 局部 knnLocate 失败回退全图 matchLocate (BGI GetBigMapPosition 回退链)。
int32_t FeatureMatcher::matchQueryLocal(IImageBuffer* query,
                                        int32_t roiX, int32_t roiY, int32_t roiW, int32_t roiH,
                                        int32_t expandCells) {
  AVOX_CV_TRY;
  results.clear();
  matchTimeMs = 0.0f;
  lastError.clear();
  if (!query) {
    lastError = "matchQueryLocal: null query";
    return 0;
  }
  if (!train.valid) {
    lastError = "matchQueryLocal: train not loaded";
    return 0;
  }
  auto start = std::chrono::steady_clock::now();
  cv::Mat queryBgr = imageBufferToBgr(query);
  if (queryBgr.empty()) {
    lastError = "matchQueryLocal: unsupported query format";
    return 0;
  }
  cv::Mat queryGray;
  cv::cvtColor(queryBgr, queryGray, cv::COLOR_BGR2GRAY);   // 对齐 BGI greyBigMapMat
  cv::Ptr<cv::Feature2D> det = cv::SIFT::create();          // 无参 (BGI SiftMatcher)
  std::vector<cv::KeyPoint> qKps;
  cv::Mat qDesc;
  det->detectAndCompute(queryGray, cv::noArray(), qKps, qDesc);
  cv::Point2d center;
  double score = 0.0;
  bool ok = false;
  std::vector<cv::KeyPoint> tKps;
  cv::Mat tDesc;
  if (mergeTrainFeaturesInRange(cv::Rect(roiX, roiY, roiW, roiH), expandCells, tKps, tDesc)) {
    ok = knnLocate(qKps, qDesc, queryGray.cols, queryGray.rows, tKps, tDesc, center, score);
  }
  if (!ok) {   // 局部失败 → 全图 Match 兜底 (BGI: result==default → Match)
    ok = matchLocate(qKps, qDesc, queryGray.cols, queryGray.rows,
                     train.allKps, train.allDesc, center, score);
  }
  matchTimeMs = std::chrono::duration<float, std::milli>(
                    std::chrono::steady_clock::now() - start)
                    .count();
  if (!ok) {
    lastError = "matchQueryLocal: locate failed";
    return 0;
  }
  FeatureMatchResult r;
  r.x = static_cast<int32_t>(std::round(center.x));
  r.y = static_cast<int32_t>(std::round(center.y));
  r.w = 0;   // 点结果哨兵
  r.h = 0;
  r.refIndex = -1;
  r.score = score;
  results.push_back(r);
  return 1;
  AVOX_CV_CATCH_RET(0);
}

// query(视口) 全图兜底定位 (BGI Match, 首轮无 expected 时用)。
int32_t FeatureMatcher::matchQueryFull(IImageBuffer* query) {
  AVOX_CV_TRY;
  results.clear();
  matchTimeMs = 0.0f;
  lastError.clear();
  if (!query) {
    lastError = "matchQueryFull: null query";
    return 0;
  }
  if (!train.valid) {
    lastError = "matchQueryFull: train not loaded";
    return 0;
  }
  auto start = std::chrono::steady_clock::now();
  cv::Mat queryBgr = imageBufferToBgr(query);
  if (queryBgr.empty()) {
    lastError = "matchQueryFull: unsupported query format";
    return 0;
  }
  cv::Mat queryGray;
  cv::cvtColor(queryBgr, queryGray, cv::COLOR_BGR2GRAY);
  cv::Ptr<cv::Feature2D> det = cv::SIFT::create();
  std::vector<cv::KeyPoint> qKps;
  cv::Mat qDesc;
  det->detectAndCompute(queryGray, cv::noArray(), qKps, qDesc);
  cv::Point2d center;
  double score = 0.0;
  bool ok = matchLocate(qKps, qDesc, queryGray.cols, queryGray.rows,
                        train.allKps, train.allDesc, center, score);
  matchTimeMs = std::chrono::duration<float, std::milli>(
                    std::chrono::steady_clock::now() - start)
                    .count();
  if (!ok) {
    lastError = "matchQueryFull: locate failed";
    return 0;
  }
  FeatureMatchResult r;
  r.x = static_cast<int32_t>(std::round(center.x));
  r.y = static_cast<int32_t>(std::round(center.y));
  r.w = 0;
  r.h = 0;
  r.refIndex = -1;
  r.score = score;
  results.push_back(r);
  return 1;
  AVOX_CV_CATCH_RET(0);
}

}
