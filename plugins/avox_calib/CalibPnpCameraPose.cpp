#include "CalibPnpCameraPose.hpp"

#include <opencv2/calib3d.hpp>

#include "avox/module/LogHelper.hpp"

namespace avox {

CalibPnpCameraPose::CalibPnpCameraPose() {
  projectPoints = std::make_unique<CalibProjectPoints>();
}

void CalibPnpCameraPose::setLensModel(const LensModel& lensModel, IImagePoints* imagePoints_) {
  imageSize = lensModel.imageSize;
  calibLensModelToCV(lensModel, cameraMatrix, distCoeffs);
  imagePoints = dynamic_cast<CalibImagePoints*>(imagePoints_);
  if (!imagePoints) {
    lastError = "imagePoints must create from imagePointsHub";
    LOGFLF(LogLevel::warn, lastError.c_str());
    return;
  }
  projectPoints->updateLensModel(cameraMatrix, distCoeffs);
  bPreFind = false;
}

bool CalibPnpCameraPose::getCamPose(IImageBuffer* image, Mat4x4d& cameraPose,
                                    CalibOffset* offset) {
  AVOX_CALIB_TRY;
  ImageFormat fmt = image->getImageFormat();
  if ((double)fmt.width != imageSize.x || (double)fmt.height != imageSize.y) {
    lastError = "pnp image size not match lensModel";
    LOGFLF(LogLevel::warn, lastError.c_str());
    return false;
  }
  cv::Mat mat = calibImage2Mat(image);
  bPreFind = imagePoints->computeCameraPose(mat, corners, cpoints, cameraMatrix, distCoeffs, rot,
                                            tra);
  if (!bPreFind) {
    return false;
  }
  if (offset != nullptr) {
    projectPoints->updatePoints(cpoints);
    projectPoints->updateCameraPose(rot, tra);
    cv::Point2f coffset = projectPoints->projectOffset(corners);
    offset->avg = coffset.x;
    offset->max = coffset.y;
  }
  // 相机在角点(标定板)坐标系下姿态 = target2camera 的逆
  cv::Mat poseMat = calibHomogeneousInverse(rot, tra);
  calibMat2Mat4x4(poseMat, cameraPose);
  return true;
  AVOX_CALIB_CATCH_RET(false)
}

bool CalibPnpCameraPose::getCamOffset(IImageBuffer* image, const Mat4x4d& cameraPose,
                                      CalibOffset& offset) {
  AVOX_CALIB_TRY;
  ImageFormat fmt = image->getImageFormat();
  if ((double)fmt.width != imageSize.x || (double)fmt.height != imageSize.y) {
    lastError = "pnp image size not match lensModel";
    LOGFLF(LogLevel::warn, lastError.c_str());
    return false;
  }
  cv::Mat mat = calibImage2Mat(image);
  if (!imagePoints->findCorners(mat, corners, cpoints)) {
    return false;
  }
  projectPoints->updatePoints(cpoints);
  Mat4x4d target2Camera = cameraPose.inverse();
  projectPoints->updateCameraPose(target2Camera);
  cv::Point2f coffset = projectPoints->projectOffset(corners);
  offset.avg = coffset.x;
  offset.max = coffset.y;
  return true;
  AVOX_CALIB_CATCH_RET(false)
}

CornerMass CalibPnpCameraPose::getCornerMass() { return imagePoints->getCornerMass(); }

const char* CalibPnpCameraPose::getLastError() { return lastError.c_str(); }

}  // namespace avox
