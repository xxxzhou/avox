#pragma once

#include "avox/AvoxVision.h"

#include <opencv2/opencv.hpp>

#include <string>
#include <vector>

namespace avox {

// IOrientationDetector 的 OpenCV 实现: BGI CameraOrientationFromGia 忠实移植。
// 由 OpencvModule 注册为工厂 "opencv", 业务经 orientationDetectorHub.create("opencv") 取实例。
// 算法: 灰度→GaussianBlur→warpPolar(行=角度,列=半径)→ROI 半径带 10-80→Rotate90CCW→
// Scharr dx→FindPeaks 左右峰→双层卷积→maxIndex+45。输出为 BGI 原始角度约定(0=右/东,
// 顺时针, 实际取值 [45,360]); setAngleRange/setSmooth 不适用(BGI 固定算法, 被忽略)。
class OrientationDetector : public IOrientationDetector {
 private:
  int32_t cx = 0;
  int32_t cy = 0;
  int32_t radius = 0;
  bool hasCircle = false;
  int32_t angleMin = 0;    // [angleMin, angleMax), 北顺时针约定
  int32_t angleMax = 360;
  int32_t smooth = 5;      // 圆周平滑窗口 (度)
  double lastAngle = -1.0;
  double lastScore = 0.0;
  float matchTimeMs = 0.0f;
  std::string lastError;

 public:
  OrientationDetector();
  ~OrientationDetector() override;

 public:
  void setCircle(int32_t cx_, int32_t cy_, int32_t radius_) override;
  void setAngleRange(int32_t angleMin_, int32_t angleMax_) override;
  void setSmooth(int32_t degrees) override;
  double compute(IImageBuffer* scene) override;
  double getLastScore() override;
  float getMatchTimeMs() override;
  const char* getLastError() override;
};

}
