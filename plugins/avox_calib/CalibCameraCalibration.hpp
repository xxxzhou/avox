#pragma once

// 内参/畸变标定 (移植自 aoce_opencv/CameraCalibration)
// N 张不同姿态图案 → cv::calibrateCamera, 每帧位姿与误差供手眼初值/筛选

#include <memory>
#include <opencv2/calib3d.hpp>
#include <opencv2/core.hpp>
#include <string>
#include <vector>

#include "CalibHelper.hpp"
#include "CalibImagePoints.hpp"

namespace avox {

// 单帧识别结果
struct CalibCornerPoints {
  std::vector<cv::Point2f> corners;
  std::vector<cv::Point3f> points;
  // 解算后: 相机在标定板系下姿态 (cv 4x4)
  cv::Mat pose;
  // avg/max 重投影误差 (像素)
  cv::Point2f offset = {};
};

// 重投影误差计算器 (内参 + 位姿 + 3D 点 → projectPoints 误差)
class CalibProjectPoints {
 private:
  cv::Mat cameraMatrix;
  cv::Mat distCoeffs;
  std::vector<cv::Point3f> targetPoints;
  cv::Mat rVec;
  cv::Mat tVec;
  std::vector<cv::Point2f> computePoints;

 public:
  void updateLensModel(const LensModel& lensModel) {
    calibLensModelToCV(lensModel, cameraMatrix, distCoeffs);
  }
  void updateLensModel(const cv::Mat& cameraMat, const cv::Mat& distCoeffsMat) {
    cameraMat.copyTo(cameraMatrix);
    distCoeffsMat.copyTo(distCoeffs);
  }
  void updatePoints(const std::vector<cv::Point3f>& points) { targetPoints = points; }
  // 位姿用 4x4 (target2camera)
  void updateCameraPose(const Mat4x4d& transform) {
    rVec = cv::Mat::eye(3, 3, CV_64F);
    tVec = cv::Mat::zeros(3, 1, CV_64F);
    calibMat4x42Mat(transform, rVec, tVec);
  }
  void updateCameraPose(const cv::Mat& rotMat, const cv::Mat& transMat) {
    rotMat.copyTo(rVec);
    transMat.copyTo(tVec);
  }
  void updateCameraPose(const cv::Mat& transform) {
    rVec = transform(cv::Rect(0, 0, 3, 3));
    tVec = transform(cv::Rect(3, 0, 1, 3));
  }
  // 投影 3D 点并返回与真实 UV 的 (平均, 最大) 像素误差
  cv::Point2f projectOffset(const std::vector<cv::Point2f>& truthPoints) {
    std::vector<cv::Point2f> points;
    cv::projectPoints(targetPoints, rVec, tVec, cameraMatrix, distCoeffs, points);
    return calibCompareOffset(truthPoints, points);
  }
};

class CalibCameraCalibration : public ICameraCalibration {
 private:
  std::vector<CalibCornerPoints> mats;
  CalibImagePoints* imagePoints = nullptr;
  vec2d imageSize = vec2d(0.0, 0.0);
  std::unique_ptr<CalibProjectPoints> drawPoints;
  std::string lastError;

 public:
  CalibCameraCalibration();
  virtual ~CalibCameraCalibration() = default;

 public:
  virtual void setImagePoints(const vec2d& imageSize, IImagePoints* imagePoints) override;
  virtual void reset() override;
  virtual int32_t saveCornerImage(IImageBuffer* image) override;
  virtual int32_t removeFrame(int32_t frameIndex) override;
  virtual float calibration(LensModel& lensModel, bool guess = false) override;
  virtual bool getFrameOffset(int32_t frameIndex, FrameOffset& frameOffset) override;
  virtual const char* getLastError() override;
};

}  // namespace avox
