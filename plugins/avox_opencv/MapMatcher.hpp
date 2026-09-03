#pragma once

#include "avox/AvoxVision.h"

#include <opencv2/opencv.hpp>

#include <string>

namespace avox {

// IMapMatcher 的 OpenCV 实现: 通用小地图定位引擎
// 1. 粗匹配: 小地图缩放到 coarseSize×coarseSize, 彩图 SQDIFF
//    (FastSqDiff: 互相关 CCORR 加速 + 源图各通道平方预计算, 对照 BGI FastSqDiffMatcher)
// 2. 精匹配: 以粗匹配位置为中心, 灰度图 matchTemplate(SQDIFF_NORMED)
// 3. 亚像素: 精匹配结果 3×3 邻域二次曲面拟合 (对照 BGI SubPixMatch)
// 通用: 不做朝向检测/掩码生成/坐标换算; 掩码经 setMask 由上层传入, 朝向用 IOrientationDetector
class MapMatcher : public IMapMatcher {
 private:
  // 粗匹配 (对照 BGI FastSqDiffMatcher)
  bool roughMatch(const cv::Mat& bgrMinimap, cv::Point2f& outPos, double& outVal);
  // 精匹配 (+ 亚像素)
  bool exactMatch(const cv::Mat& bgrMinimap, const cv::Point2f& roughPos,
                  cv::Point2f& outPos, double& outVal);
  // 亚像素拟合 (对照 BGI SubPixMatch.Fit): 3×3 邻域二次曲面驻点, 返回亚像素 loc
  cv::Point2f subPixelFit(const cv::Mat& resultImg, const cv::Point& loc);
  // 置信度归一化 (SQDIFF_NORMED → score = 1 - val, 钳 [0,1])
  double normalizeScore(double sqDiffVal);
  // IImageBuffer → cv::Mat BGR
  static cv::Mat imageBufferToBgr(IImageBuffer* buf);

 private:
  // 配置
  double minScore = 0.95;
  int coarseSize = 52;      // BGI: RoughSize = 52
  int exactSize = 260;      // BGI: ExactSize = 260
  int searchRadius = 50;    // 精匹配局部搜索半径 (fineMap 像素)
  int roughSearchRadius = 0;  // 粗匹配局部搜索半径 (coarseMap 像素, 0=不限制)
  bool useSubPixel = true;  // 亚像素拟合开关
  bool useRoi = false;
  bool hasMask = false;
  bool hasPrevPos = false;
  bool useAutoRoi = false;
  cv::Rect roi;
  cv::Rect autoRoi;
  cv::Point2f prevPos;      // 上次匹配位置 (coarseMap 像素坐标)
  // 用户掩码 (setMask 设入, 单通道 0/255); hasMask=false 时为空 = 全参与
  cv::Mat userMaskMat;

  // 地图图像 (懒加载)
  cv::Mat coarseMap;   // 彩图, 粗匹配用 (BGI *_color.webp)
  cv::Mat fineMap;     // 灰度图, 精匹配用 (BGI *_gray.webp)
  // 预计算: 彩图各通道 + 各通道平方 (对照 BGI FastSqDiffMatcher)
  std::vector<cv::Mat> coarseMapChannels;
  std::vector<cv::Mat> coarseMapChannelsSq;
  // 粗→精坐标缩放 (fineMap 尺寸 / coarseMap 尺寸)
  double coarseToFineScaleX = 1.0;
  double coarseToFineScaleY = 1.0;

  // 结果
  MapMatchResult result;
  float matchTimeMs = 0.0f;
  std::string lastError;

 public:
  MapMatcher();
  ~MapMatcher() override;

 public:
  void setMapImage(IImageBuffer* mapImg) override;
  void setMapImageByPath(const char* path) override;
  void setFineMapImage(IImageBuffer* mapImg) override;
  void setFineMapImageByPath(const char* path) override;
  void setRoi(int32_t x, int32_t y, int32_t w, int32_t h) override;
  void clearRoi() override;
  void setMask(IImageBuffer* mask) override;
  void clearMask() override;
  void setMinScore(double score) override;
  void setCoarseSize(int32_t size) override;
  void setExactSize(int32_t size) override;
  void setSearchRadius(int32_t radius) override;
  void setRoughSearchRadius(int32_t radius) override;
  void setSubPixel(bool enable) override;
  void setPrevPosition(double px, double py) override;
  void clearPrevPosition() override;
  int32_t match(IImageBuffer* minimap) override;
  // match 实现（match = try/catch 装甲壳，cv 异常不 terminate 宿主进程）
  int32_t matchImpl(IImageBuffer* minimap);
  bool getResult(MapMatchResult* out) override;
  float getMatchTimeMs() override;
  const char* getLastError() override;
};

}
