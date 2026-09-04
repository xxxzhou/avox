#pragma once

// PnP 实时反推相机姿态 (移植自 aoce_opencv/PnPCameraPose)
// 已知内参 + 幕墙 3D 点, 每帧画面反推相机在标定板(幕墙)坐标系下姿态,
// 用于运行时验证标定结果 / 检查追踪精度

#include <memory>
#include <opencv2/core.hpp>
#include <string>
#include <vector>

#include "CalibCameraCalibration.hpp"  // CalibProjectPoints
#include "CalibHelper.hpp"
#include "CalibImagePoints.hpp"

namespace avox {

class CalibPnpCameraPose : public IPnpCameraPose {
 private:
  vec2d imageSize = vec2d(0.0, 0.0);
  CalibImagePoints* imagePoints = nullptr;
  cv::Mat cameraMatrix;
  cv::Mat distCoeffs;
  std::unique_ptr<CalibProjectPoints> projectPoints;
  // 避免频繁申请
  std::vector<cv::Point2f> corners;
  std::vector<cv::Point3f> cpoints;
  cv::Mat rot;
  cv::Mat tra;
  bool bPreFind = false;
  std::string lastError;

 public:
  CalibPnpCameraPose();
  virtual ~CalibPnpCameraPose() = default;

 public:
  virtual void setLensModel(const LensModel& lensModel, IImagePoints* imagePoints) override;
  virtual bool getCamPose(IImageBuffer* image, Mat4x4d& cameraPose,
                          CalibOffset* offset = nullptr) override;
  virtual bool getCamOffset(IImageBuffer* image, const Mat4x4d& cameraPose,
                            CalibOffset& offset) override;
  virtual CornerMass getCornerMass() override;
  virtual const char* getLastError() override;
};

}  // namespace avox
