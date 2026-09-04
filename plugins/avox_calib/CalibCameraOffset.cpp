#include "CalibCameraOffset.hpp"

#include <cmath>
#include <opencv2/calib3d.hpp>

#include "avox/module/LogHelper.hpp"

namespace avox {

CalibCameraOffset::CalibCameraOffset() {
  drawPoints = std::make_unique<CalibProjectPoints>();
  base2targetQ = cv::Mat::eye(4, 4, CV_64F);
  camera2trackQ = cv::Mat::eye(4, 4, CV_64F);
}

void CalibCameraOffset::setLensModel(const LensModel& lensModel, IImagePoints* imagePoints_) {
  imageSize = lensModel.imageSize;
  calibLensModelToCV(lensModel, cameraMatrix, distCoeffs);
  imagePoints = dynamic_cast<CalibImagePoints*>(imagePoints_);
  if (!imagePoints) {
    lastError = "imagePoints must create from imagePointsHub";
    LOGFLF(LogLevel::warn, lastError.c_str());
    return;
  }
  drawPoints->updateLensModel(cameraMatrix, distCoeffs);
  drawPoints->updatePoints(imagePoints->getPoints());
  reset();
}

void CalibCameraOffset::reset() {
  bCompute = false;
  scale = 1.0;
  matPoses.clear();
  computePoses.clear();
}

int32_t CalibCameraOffset::saveTrackCornerImage(const Mat4x4d& trackPose, IImageBuffer* image) {
  AVOX_CALIB_TRY;
  if (!imagePoints || imagePoints->getPointCount() <= 0) {
    lastError = "must setLensModel first";
    return -1;
  }
  ImageFormat fmt = image->getImageFormat();
  if ((double)fmt.width != imageSize.x || (double)fmt.height != imageSize.y) {
    lastError = "camera offset image size not match";
    LOGFLF(LogLevel::warn, lastError.c_str());
    return -1;
  }
  bCompute = false;
  cv::Mat mat = calibImage2Mat(image);
  CalibMatPose matPose;
  if (!getMatPose(mat, matPose)) {
    return -1;
  }
  // 追踪器位姿转 cv, 全程 double 保精度
  matPose.track2baseR = cv::Mat::zeros(3, 3, CV_64F);
  matPose.track2baseT = cv::Mat::zeros(3, 1, CV_64F);
  calibMat4x42Mat(trackPose, matPose.track2baseR, matPose.track2baseT);
  matPoses.push_back(matPose);
  return (int32_t)matPoses.size();
  AVOX_CALIB_CATCH_RET(-1)
}

int32_t CalibCameraOffset::saveTrackPoseDirect(const CalibMatPose& matPose) {
  matPoses.push_back(matPose);
  return (int32_t)matPoses.size();
}

bool CalibCameraOffset::getMatPose(cv::Mat& mat, CalibMatPose& matPose) {
  if (!imagePoints || imagePoints->getPoints().empty()) {
    return false;
  }
  cv::Mat rot, tra;
  std::vector<cv::Point2f> corners;
  std::vector<cv::Point3f> points;
  if (!imagePoints->computeCameraPose(mat, corners, points, cameraMatrix, distCoeffs, rot, tra)) {
    return false;
  }
  cv::Mat rotMat;
  cv::Rodrigues(rot, rotMat);
  // PnP 位姿重投影误差 (内参级, 应在 0.x 像素级)
  drawPoints->updateCameraPose(rotMat, tra);
  drawPoints->updatePoints(points);
  matPose.innerOffset = drawPoints->projectOffset(corners);
  matPose.target2camR = rotMat;
  matPose.target2camT = tra;
  matPose.corners = corners;
  matPose.points = points;
  return true;
}

int32_t CalibCameraOffset::removeFrame(int32_t frameIndex) {
  if (frameIndex >= (int32_t)matPoses.size() || frameIndex < 0) {
    return (int32_t)matPoses.size();
  }
  matPoses.erase(matPoses.begin() + frameIndex);
  return (int32_t)matPoses.size();
}

void CalibCameraOffset::handEye(const std::vector<CalibMatPose>& poses, cv::Mat& camera2track,
                                cv::Mat& base2target, double& scale_) {
  track2baseR.clear();
  track2baseT.clear();
  target2camR.clear();
  target2camT.clear();
  const size_t size = poses.size();
  for (size_t i = 0; i < size; i++) {
    target2camR.push_back(poses[i].target2camR);
    target2camT.push_back(poses[i].target2camT);
    track2baseR.push_back(poses[i].track2baseR);
    track2baseT.push_back(poses[i].track2baseT);
  }
  cv::Mat camera2trackR;
  cv::Mat camera2trackT;
  if (bScale) {
    calibrateHandEyeScale(track2baseR, track2baseT, target2camR, target2camT, camera2trackR,
                          camera2trackT);
    // Tcg = [tx,ty,tz,Scale], 真实平移 = 前三分量 * Scale
    scale_ = 1.0 / camera2trackT.at<double>(3);
  } else {
    // 默认 Tsai (CALIB_HAND_EYE_TSAI)
    cv::calibrateHandEye(track2baseR, track2baseT, target2camR, target2camT, camera2trackR,
                         camera2trackT);
    scale_ = 1.0;
  }
  // 平移统一到现实尺寸 (误差计算尺度一致)
  cv::Mat scaledT = camera2trackT(cv::Rect(0, 0, 1, 3)) * scale_;
  calibMat2Mat4x4(camera2trackR, scaledT, camera2track);
  // SVD 求 base2target: target2camera · base2target = base2camera
  // A(N·4,4)·X(4,4) = B(N·4,4), X = (AᵀA)⁻¹AᵀB 为 N 组关系最小二乘最优解
  cv::Mat base2cameras(4 * (int)size, 4, CV_64FC1);
  cv::Mat target2cameras(4 * (int)size, 4, CV_64FC1);
  for (size_t i = 0; i < size; i++) {
    cv::Mat track2base(4, 4, CV_64FC1);
    cv::Mat target2camera(4, 4, CV_64FC1);
    calibMat2Mat4x4(poses[i].track2baseR, poses[i].track2baseT * scale_, track2base);
    // base2camera = (track2base · camera2track)⁻¹
    cv::Mat base2camera = calibHomogeneousInverse(track2base * camera2track);
    calibMat2Mat4x4(poses[i].target2camR, poses[i].target2camT, target2camera);
    base2camera.copyTo(base2cameras(cv::Rect(0, (int)i * 4, 4, 4)));
    target2camera.copyTo(target2cameras(cv::Rect(0, (int)i * 4, 4, 4)));
  }
  cv::Mat target2camerasT;
  cv::transpose(target2cameras, target2camerasT);
  base2target = (target2camerasT * target2cameras).inv() * target2camerasT * base2cameras;
  // 近似旋转矩阵转标准正交旋转矩阵
  cv::Mat b2tRot = base2target(cv::Rect(0, 0, 3, 3));
  calibNormalRot(b2tRot);
  b2tRot.copyTo(base2target(cv::Rect(0, 0, 3, 3)));
  double pw = base2target.at<double>(3, 3);
  base2target.at<double>(0, 3) /= pw;
  base2target.at<double>(1, 3) /= pw;
  base2target.at<double>(2, 3) /= pw;
  base2target.at<double>(3, 0) = 0.0;
  base2target.at<double>(3, 1) = 0.0;
  base2target.at<double>(3, 2) = 0.0;
  base2target.at<double>(3, 3) = 1.0;
}

void CalibCameraOffset::selectOne(const std::vector<int32_t>& indexs) {
  tMatPoses.clear();
  for (size_t i = 0; i < indexs.size(); ++i) {
    tMatPoses.push_back(computePoses[indexs[i]]);
  }
  cv::Mat camera2track;
  cv::Mat base2target;
  double cscale = 1.0;
  handEye(tMatPoses, camera2track, base2target, cscale);
  // 用该组合结果验证全部数据 (含未参与组合的帧)
  cv::Point2f totalOffset = {};
  int32_t validateSize = (int32_t)computePoses.size();
  for (int32_t i = 0; i < validateSize; i++) {
    cv::Mat track2base(4, 4, CV_64FC1);
    calibMat2Mat4x4(computePoses[i].track2baseR, computePoses[i].track2baseT * cscale, track2base);
    // 摄像机在标定板系下姿态 = base2target · track2base · camera2track
    cv::Mat camera2target = base2target * track2base * camera2track;
    cv::Mat target2camera = calibHomogeneousInverse(camera2target);
    drawPoints->updateCameraPose(target2camera);
    drawPoints->updatePoints(computePoses[i].points);
    totalOffset += drawPoints->projectOffset(computePoses[i].corners);
  }
  totalOffset = totalOffset / (float)validateSize;
  // 记录最优拟合
  if (totalOffset.x < minOffset.x) {
    minOffset = totalOffset;
    camera2track.copyTo(camera2trackQ);
    base2target.copyTo(base2targetQ);
    scale = cscale;
    minIndexs = indexs;
  }
}

void CalibCameraOffset::selectAll(int32_t k) {
  // C(N,k) 字典序遍历
  int32_t n = (int32_t)computePoses.size();
  std::vector<int32_t> selects(k);
  for (int i = 0; i < k; ++i) {
    selects[i] = i;
  }
  selectOne(selects);
  while (true) {
    int i = k - 1;
    while (i >= 0 && selects[i] == n - k + i) {
      --i;
    }
    if (i < 0) {
      break;
    }
    ++selects[i];
    for (int j = i + 1; j < k; ++j) {
      selects[j] = selects[j - 1] + 1;
    }
    selectOne(selects);
  }
}

int32_t CalibCameraOffset::compute(const OffsetParamet& paramet, CameraTrackOffset& cameraOffset) {
  AVOX_CALIB_TRY;
  computePoses.clear();
  newIndexs.clear();
  minIndexs.clear();
  std::vector<int32_t> noIndexs;
  for (size_t i = 0; i < matPoses.size(); i++) {
    uint64_t bitIndex = 1ull << i;
    if (paramet.computeIndexs <= 0 || (bitIndex & paramet.computeIndexs) > 0) {
      computePoses.push_back(matPoses[i]);
      newIndexs.push_back((int32_t)i);
    } else {
      noIndexs.push_back((int32_t)i);
    }
  }
  int32_t totalSize = (int32_t)computePoses.size();
  if (totalSize < paramet.startIndex) {
    cameraOffset.cameraTrack.scale = 1.0;
    cameraOffset.offset = 0.0f;
    lastError = "please record at least " + std::to_string(paramet.startIndex) + " images";
    LOGFLF(LogLevel::warn, lastError.c_str());
    return totalSize;
  }
  bScale = paramet.bScale;
  float maxValue = std::numeric_limits<float>::max();
  minOffset = {maxValue, maxValue};
  // 全组合搜索最优 (经验: 最优解在 3-5 帧组合中)
  int32_t startIndex = std::max(minSize, paramet.startIndex);
  int32_t range = std::max(0, paramet.computeRange);
  for (int32_t i = startIndex; i <= startIndex + range; i++) {
    if (i > totalSize) {
      break;
    }
    selectAll(i);
  }
  cameraOffset.cameraTrack.scale = scale;
  calibMat2Mat4x4(camera2trackQ, cameraOffset.cameraTrack.camera2track);
  calibMat2Mat4x4(base2targetQ, cameraOffset.cameraTrack.base2target);
  cameraOffset.offset = minOffset.x;
  // suggest 是 matPoses 索引的位掩码
  cameraOffset.suggest = 0;
  for (size_t i = 0; i < minIndexs.size(); i++) {
    cameraOffset.suggest += (1ull << newIndexs[minIndexs[i]]);
  }
  bCompute = true;
  return totalSize;
  AVOX_CALIB_CATCH_RET(0)
}

bool CalibCameraOffset::getFrameOffset(int32_t frameIndex, FrameOffset& trackOffset,
                                       FrameOffset& innerOffset) {
  AVOX_CALIB_TRY;
  if (frameIndex >= (int32_t)matPoses.size() || frameIndex < 0) {
    return false;
  }
  CalibMatPose& pose = matPoses[frameIndex];
  cv::Mat track2base(4, 4, CV_64FC1);
  calibMat2Mat4x4(pose.track2baseR, pose.track2baseT * scale, track2base);
  cv::Mat camera2target = base2targetQ * track2base * camera2trackQ;
  calibMat2Mat4x4(camera2target, trackOffset.cameraPose);
  // Track 级误差: 换算位姿重投影 (结果缓存于 pose.offset)
  if (pose.offset.x <= 0.0f && !pose.corners.empty()) {
    cv::Mat target2camera = calibHomogeneousInverse(camera2target);
    drawPoints->updateCameraPose(target2camera);
    drawPoints->updatePoints(pose.points);
    pose.offset = drawPoints->projectOffset(pose.corners);
  }
  trackOffset.offset.avg = pose.offset.x;
  trackOffset.offset.max = pose.offset.y;
  calibMat2Mat4x4(calibHomogeneousInverse(pose.target2camR, pose.target2camT),
                  innerOffset.cameraPose);
  innerOffset.offset.avg = pose.innerOffset.x;
  innerOffset.offset.max = pose.innerOffset.y;
  return true;
  AVOX_CALIB_CATCH_RET(false)
}

const char* CalibCameraOffset::getLastError() { return lastError.c_str(); }

}  // namespace avox
