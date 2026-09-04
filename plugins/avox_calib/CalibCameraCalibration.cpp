#include "CalibCameraCalibration.hpp"

#include <opencv2/calib3d.hpp>

#include "avox/module/LogHelper.hpp"

namespace avox {

CalibCameraCalibration::CalibCameraCalibration() {
  drawPoints = std::make_unique<CalibProjectPoints>();
}

void CalibCameraCalibration::setImagePoints(const vec2d& imageSize_,
                                            IImagePoints* imagePoints_) {
  imageSize = imageSize_;
  imagePoints = dynamic_cast<CalibImagePoints*>(imagePoints_);
  if (!imagePoints) {
    lastError = "imagePoints must create from imagePointsHub";
    LOGFLF(LogLevel::warn, lastError.c_str());
    return;
  }
  reset();
}

void CalibCameraCalibration::reset() { mats.clear(); }

int32_t CalibCameraCalibration::saveCornerImage(IImageBuffer* image) {
  AVOX_CALIB_TRY;
  if (!imagePoints || imagePoints->getPointCount() <= 0) {
    lastError = "must setImagePoints first";
    return -1;
  }
  ImageFormat fmt = image->getImageFormat();
  if ((double)fmt.width != imageSize.x || (double)fmt.height != imageSize.y) {
    lastError = "calibration image size not match";
    LOGFLF(LogLevel::warn, lastError.c_str());
    return -1;
  }
  cv::Mat mat = calibImage2Mat(image);
  std::vector<cv::Point2f> corners;
  std::vector<cv::Point3f> points;
  if (!imagePoints->findCorners(mat, corners, points)) {
    return -1;
  }
  CalibCornerPoints cornerPoint;
  cornerPoint.corners = corners;
  cornerPoint.points = points;
  mats.push_back(cornerPoint);
  return (int32_t)mats.size();
  AVOX_CALIB_CATCH_RET(-1)
}

int32_t CalibCameraCalibration::removeFrame(int32_t frameIndex) {
  if (frameIndex >= (int32_t)mats.size() || frameIndex < 0) {
    return (int32_t)mats.size();
  }
  mats.erase(mats.begin() + frameIndex);
  return (int32_t)mats.size();
}

bool CalibCameraCalibration::getFrameOffset(int32_t frameIndex, FrameOffset& frameOffset) {
  if (frameIndex >= (int32_t)mats.size() || frameIndex < 0 || mats[frameIndex].pose.empty()) {
    return false;
  }
  calibMat2Mat4x4(mats[frameIndex].pose, frameOffset.cameraPose);
  frameOffset.offset.avg = mats[frameIndex].offset.x;
  frameOffset.offset.max = mats[frameIndex].offset.y;
  return true;
}

float CalibCameraCalibration::calibration(LensModel& lensModel, bool guess) {
  AVOX_CALIB_TRY;
  if (mats.size() < 3) {
    lastError = "please save at least 3 corner images";
    return -1.0f;
  }
  cv::Mat cameraMatrix;
  cv::Mat distCoeffs;
  std::vector<cv::Mat> rvecs;
  std::vector<cv::Mat> tvecs;
  std::vector<std::vector<cv::Point2f>> samples2d;
  std::vector<std::vector<cv::Point3f>> samples3d;
  for (const CalibCornerPoints& cornerPoints : mats) {
    samples2d.push_back(cornerPoints.corners);
    samples3d.push_back(cornerPoints.points);
  }
  int32_t flags = 0;
  if (guess && lensModel.focalLength.x > 0 && lensModel.focalLength.y > 0 &&
      lensModel.imageSize.x > 0) {
    calibLensModelToCV(lensModel, cameraMatrix, distCoeffs);
  } else {
    // 非平面点集下 calibrateCamera 的内参初始化(逐视图单应)不可用,
    // 恒给默认初值 (fx=图宽, 中心 0.5), 同 aoce VideoCalibration 做法
    cameraMatrix = cv::Mat::eye(3, 3, CV_64F);
    distCoeffs = cv::Mat::zeros(5, 1, CV_64F);
    cameraMatrix.at<double>(0, 0) = imageSize.x;
    cameraMatrix.at<double>(1, 1) = imageSize.x;
    cameraMatrix.at<double>(0, 2) = 0.5 * imageSize.x;
    cameraMatrix.at<double>(1, 2) = 0.5 * imageSize.y;
  }
  flags = cv::CALIB_USE_INTRINSIC_GUESS;
  double outError = 0.0;
  // 奇异/坏数据时 cv 内部抛异常, 归因到 lastError 后失败返回
  outError = cv::calibrateCamera(samples3d, samples2d, cv::Size(imageSize.x, imageSize.y),
                                 cameraMatrix, distCoeffs, rvecs, tvecs, flags);
  calibLensCVToModel(cameraMatrix, distCoeffs, imageSize, lensModel);
  LOGFLF(LogLevel::info, "calibration lens offset: ", (float)outError);
  // 逐帧位姿与误差
  drawPoints->updateLensModel(cameraMatrix, distCoeffs);
  for (size_t i = 0; i < mats.size(); i++) {
    drawPoints->updateCameraPose(rvecs[i], tvecs[i]);
    // aruco 每帧 3D 点不同
    drawPoints->updatePoints(mats[i].points);
    mats[i].offset = drawPoints->projectOffset(mats[i].corners);
    // 相机在标定板系下位姿 = target2camera 的逆
    mats[i].pose = calibHomogeneousInverse(rvecs[i], tvecs[i]);
  }
  return (float)outError;
  AVOX_CALIB_CATCH_RET(-1.0f)
}

const char* CalibCameraCalibration::getLastError() { return lastError.c_str(); }

}  // namespace avox
