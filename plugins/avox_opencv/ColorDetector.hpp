#pragma once

#include "avox/AvoxVision.h"

#include <opencv2/opencv.hpp>

#include <string>
#include <vector>

namespace avox {

// IColorDetector 的 OpenCV 实现: cv::inRange + connectedComponentsWithStats。
// 由 OpencvModule 注册为工厂 "opencv", 业务经 colorDetectorHub.create("opencv") 取实例。
// 在 scene 中检测落在 [lower, upper] 颜色范围内的连通区域, 返回各区域包围盒/面积/填充率。
class ColorDetector : public IColorDetector {
 private:
  ColorSpace cs = ColorSpace::bgr;
  cv::Scalar lower;          // 三通道下界
  cv::Scalar upper;          // 三通道上界
  bool hasRange = false;
  bool useRoi = false;
  cv::Rect roi;
  int32_t minArea = 8;
  int32_t maxRegions = 100;
  std::vector<ColorRegion> regions;
  float matchTimeMs = 0.0f;
  std::string lastError;

  static bool resolveIndex(int32_t count, int32_t index, int32_t& out);

 public:
  ColorDetector();
  ~ColorDetector() override;

 public:
  void setColorSpace(ColorSpace cs_) override;
  void setRange(int32_t c0Min, int32_t c1Min, int32_t c2Min,
                int32_t c0Max, int32_t c1Max, int32_t c2Max) override;
  void setRoi(int32_t x, int32_t y, int32_t w, int32_t h) override;
  void clearRoi() override;
  void setMinArea(int32_t area) override;
  void setMaxRegions(int32_t n) override;
  int32_t detect(IImageBuffer* scene) override;
  int32_t getRegionCount() override;
  bool getRegion(int32_t index, ColorRegion* out) override;
  float getMatchTimeMs() override;
  const char* getLastError() override;
};

}
