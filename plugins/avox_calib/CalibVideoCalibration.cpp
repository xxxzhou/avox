#include "CalibVideoCalibration.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>

#include "avox/module/LogHelper.hpp"

namespace avox {

CalibVideoCalibration::CalibVideoCalibration() {
  projectPoints = std::make_unique<CalibProjectPoints>();
  minResult.cameraTrack.scale = 1.0;
}

void CalibVideoCalibration::setImagePoints(IImagePoints* imagePoints_) {
  imagePoints = dynamic_cast<CalibImagePoints*>(imagePoints_);
  if (!imagePoints) {
    lastError = "imagePoints must create from imagePointsHub";
    LOGFLF(LogLevel::warn, lastError.c_str());
    return;
  }
  reset();
}

void CalibVideoCalibration::reset() {
  framePoses.clear();
  trackPoses.clear();
  imageSize = vec2d(0.0, 0.0);
}

int32_t CalibVideoCalibration::saveImage(IImageBuffer* image) {
  AVOX_CALIB_TRY;
  if (!imagePoints) {
    lastError = "VideoCalibration::saveImage must set imagePoints";
    LOGFLF(LogLevel::warn, lastError.c_str());
    return -1;
  }
  // 以首帧尺寸为准, 后续帧自动缩放
  ImageFormat fmt = image->getImageFormat();
  if (imageSize.x == 0.0 || imageSize.y == 0.0) {
    imageSize = vec2d((double)fmt.width, (double)fmt.height);
  }
  cv::Mat mat = calibImage2Mat(image);
  if ((double)fmt.width != imageSize.x || (double)fmt.height != imageSize.y) {
    LOGFLF(LogLevel::warn, "video calibration resize image: ", fmt.width, "x", fmt.height, " -> ",
           (int)imageSize.x, "x", (int)imageSize.y);
    cv::resize(mat, mat, cv::Size((int)imageSize.x, (int)imageSize.y));
  }
  CalibFramePose framePose;
  framePose.bFind = imagePoints->findCorners(mat, framePose.corners, framePose.points);
  if (framePose.bFind) {
    if (imagePoints->getCornerType() == CalibCornerType::aruco) {
      CornerMass mass = imagePoints->getCornerMass();
      framePose.blur = mass.blur;
      framePose.visibility = mass.visibility;
    } else {
      // 棋盘格识别是二值的, 直接置满 (aoce 原版恒 0 会被默认 minBlur 过滤掉)
      framePose.blur = 1.0f;
      framePose.visibility = 1.0f;
    }
  }
  framePoses.push_back(framePose);
  return (int32_t)framePoses.size();
  AVOX_CALIB_CATCH_RET(-1)
}

void CalibVideoCalibration::saveTrackPose(const Mat4x4d& trackPose) {
  CalibTrackMat trackMat;
  trackMat.track2baseR = cv::Mat::zeros(3, 3, CV_64F);
  trackMat.track2baseT = cv::Mat::zeros(3, 1, CV_64F);
  calibMat4x42Mat(trackPose, trackMat.track2baseR, trackMat.track2baseT);
  trackPoses.push_back(trackMat);
}

int32_t CalibVideoCalibration::removeFrame(int32_t frameIndex) {
  if (frameIndex >= (int32_t)framePoses.size() || frameIndex < 0) {
    return (int32_t)framePoses.size();
  }
  framePoses.erase(framePoses.begin() + frameIndex);
  if (frameIndex < (int32_t)trackPoses.size()) {
    trackPoses.erase(trackPoses.begin() + frameIndex);
  }
  return (int32_t)framePoses.size();
}

bool CalibVideoCalibration::compute(const VideoRTParamet& videoPar, VideoResult& videoResult) {
  AVOX_CALIB_TRY;
  bool bClips = computeVideoClips(videoPar.minBlur, videoPar.minVisibility);
  if (!bClips) {
    return false;
  }
  bool bFindInner = computeCameraInner();
  if (!bFindInner) {
    return false;
  }
  videoResult.lensModel = lensModel;
  videoResult.lensOffset = (float)innerOffset;
  // 无 track 数据, 只解内参
  if (trackPoses.empty()) {
    return true;
  }
  // 手眼: 组合范围按数据量自适应控制计算量 (C(N,k) 组合爆炸)
  float offsetCount = 1.0f;
  pamerat = OffsetParamet();
  pamerat.computeRange = 2;
  pamerat.bScale = true;
  if (selectCount > 18) {
    if (!videoPar.bRange) {
      if (selectCount < 27) {
        pamerat.computeRange = 1;
      } else if (selectCount < 36) {
        pamerat.computeRange = 0;
      } else {
        pamerat.computeRange = 0;
        offsetCount = float(selectCount) / 45;
      }
    } else {
      offsetCount = float(selectCount) / 18;
    }
  }
  minResult.offset = std::numeric_limits<float>::max();
  bool bOffset = computeCameraOffset(offsetCount, 0);
  if (bOffset) {
    computeTrackPose();
  }
  LOGFLF(LogLevel::info, "VideoCalibration::compute min offset: ", minResult.offset,
         " scale: ", minResult.cameraTrack.scale);
  videoResult.offsetResult = minResult;
  return bOffset;
  AVOX_CALIB_CATCH_RET(false)
}

bool CalibVideoCalibration::computeVideoClips(float minBlur, float minVisible) {
  videoClips.clear();
  selectCount = 0;
  CalibVideoClip videoClip;
  bool bPreFind = false;
  int32_t frameSize = (int32_t)framePoses.size();
  for (int32_t i = 0; i < frameSize; i++) {
    bool bFind = framePoses[i].bFind && framePoses[i].blur >= minBlur &&
                 framePoses[i].visibility >= minVisible;
    if (bFind) {
      if (!bPreFind) {
        videoClip.startIndex = i;
        videoClip.count = 1;
      } else {
        videoClip.count++;
      }
    } else {
      if (bPreFind) {
        videoClips.push_back(videoClip);
      }
    }
    bPreFind = bFind;
  }
  if (bPreFind) {
    videoClips.push_back(videoClip);
  }
  for (const auto& clip : videoClips) {
    selectCount += clip.count;
  }
  if (selectCount < 3) {
    lastError = "video calibration usable frames less 3";
    LOGFLF(LogLevel::warn, lastError.c_str());
    return false;
  }
  return true;
}

bool CalibVideoCalibration::computeCameraInner() {
  float offsetCount = 1.0f;
  if (selectCount > maxInnerCount) {
    offsetCount = float(selectCount) / maxInnerCount;
  }
  innerOffset = 0.0;
  std::vector<cv::Mat> rvecs;
  std::vector<cv::Mat> tvecs;
  std::vector<std::vector<cv::Point2f>> samples2d;
  std::vector<std::vector<cv::Point3f>> samples3d;
  // 均匀采样
  int32_t recordIndex = 0;
  float offsetIndex = 0;
  for (const auto& clip : videoClips) {
    for (int32_t i = clip.startIndex; i < clip.startIndex + clip.count; i++) {
      if (recordIndex++ == (int32_t)offsetIndex) {
        samples2d.push_back(framePoses[i].corners);
        samples3d.push_back(framePoses[i].points);
        offsetIndex += offsetCount;
      }
    }
  }
  if (samples3d.size() < 3) {
    lastError = "video calibration usable samples less 3";
    return false;
  }
  // 初值: fx=fy=图宽, 中心 0.5 (CALIB_USE_INTRINSIC_GUESS 收敛更快)
  cameraMatrix = cv::Mat::eye(3, 3, CV_64F);
  distCoeffs = cv::Mat::zeros(5, 1, CV_64F);
  cameraMatrix.at<double>(0, 0) = imageSize.x;
  cameraMatrix.at<double>(1, 1) = imageSize.x;
  cameraMatrix.at<double>(0, 2) = 0.5 * imageSize.x;
  cameraMatrix.at<double>(1, 2) = 0.5 * imageSize.y;
  try {
    innerOffset = cv::calibrateCamera(samples3d, samples2d, cv::Size((int)imageSize.x, (int)imageSize.y),
                                      cameraMatrix, distCoeffs, rvecs, tvecs,
                                      cv::CALIB_USE_INTRINSIC_GUESS);
  } catch (const std::exception& e) {
    lastError = std::string("video calibration inner error: ") + e.what();
    LOGFLF(LogLevel::warn, lastError.c_str());
    return false;
  }
  calibLensCVToModel(cameraMatrix, distCoeffs, imageSize, lensModel);
  projectPoints->updateLensModel(cameraMatrix, distCoeffs);
  LOGFLF(LogLevel::info, "video calibration inner offset: ", (float)innerOffset);
  computeCameraPose();
  return true;
}

void CalibVideoCalibration::computeCameraPose() {
  for (const auto& clip : videoClips) {
    for (int32_t i = clip.startIndex; i < clip.startIndex + clip.count; i++) {
      CalibFramePose& framePose = framePoses[i];
      framePose.bFind = cv::solvePnP(framePose.points, framePose.corners, cameraMatrix,
                                     distCoeffs, framePose.target2camR, framePose.target2camT);
      if (framePose.target2camR.cols == 1 || framePose.target2camR.rows == 1) {
        // solvePnP 返回轴角, 转 3x3 供手眼使用
        cv::Rodrigues(framePose.target2camR, framePose.target2camR);
      }
      if (framePose.bFind) {
        projectPoints->updatePoints(framePose.points);
        projectPoints->updateCameraPose(framePose.target2camR, framePose.target2camT);
        framePose.innerOffset = projectPoints->projectOffset(framePose.corners);
      }
    }
  }
}

bool CalibVideoCalibration::computeCameraOffset(float offsetCount, int32_t delay) {
  if (videoClips.empty() || trackPoses.size() < 3) {
    lastError = "video calibration image list or track list less 3";
    return false;
  }
  // 片段与 track 共同决定的有效索引范围 (delay 为时间对齐错位)
  int32_t clipLastIndex = (int32_t)videoClips.size() - 1;
  int32_t maxClipIndex = videoClips[clipLastIndex].startIndex + videoClips[clipLastIndex].count;
  int32_t maxPIndex = std::min(maxClipIndex, (int32_t)trackPoses.size());
  std::unique_ptr<CalibCameraOffset> cameraOffset = std::make_unique<CalibCameraOffset>();
  cameraOffset->setLensModel(lensModel, imagePoints);
  cameraOffset->reset();
  int32_t recordIndex = 0;
  int32_t recordCount = 0;
  int32_t minIndex = 0;
  int32_t maxIndex = maxPIndex;
  if (delay > 0) {
    minIndex = delay;
  }
  if (delay < 0) {
    maxIndex += delay;
  }
  float offsetIndex = 0;
  std::vector<int32_t> clipIndexs;
  for (const auto& clip : videoClips) {
    int32_t startIndex = clip.startIndex;
    int32_t endIndex = clip.startIndex + clip.count;
    if (startIndex > maxIndex || endIndex < minIndex) {
      continue;
    }
    int32_t si = std::max(clip.startIndex, minIndex);
    int32_t ei = std::min(endIndex, maxIndex);
    for (int32_t j = si; j < ei; j++) {
      if (recordIndex++ == (int32_t)offsetIndex) {
        CalibFramePose& framePose = framePoses[j];
        // 直接复用 PnP 结果组帧 (不再重复识别)
        CalibMatPose matPose;
        matPose.corners = framePose.corners;
        matPose.points = framePose.points;
        matPose.target2camR = framePose.target2camR;
        matPose.target2camT = framePose.target2camT;
        matPose.innerOffset = framePose.innerOffset;
        matPose.track2baseR = trackPoses[j + delay].track2baseR;
        matPose.track2baseT = trackPoses[j + delay].track2baseT;
        recordCount = cameraOffset->saveTrackPoseDirect(matPose);
        clipIndexs.push_back(j);
        offsetIndex += offsetCount;
      }
    }
  }
  CameraTrackOffset cameraResult;
  cameraOffset->compute(pamerat, cameraResult);
  if (cameraResult.offset < minResult.offset || minResult.offset == 0) {
    minResult = cameraResult;
  }
  // suggest 从计算子集索引还原成视频帧索引位掩码
  std::vector<int32_t> selectIndexs;
  for (int32_t i = 0; i < recordCount; i++) {
    uint64_t bitIndex = 1ull << i;
    if ((minResult.suggest & bitIndex) > 0) {
      selectIndexs.push_back(clipIndexs[i]);
    }
  }
  minResult.suggest = 0;
  for (auto clipIndex : selectIndexs) {
    minResult.suggest += (1ull << clipIndex);
  }
  return true;
}

void CalibVideoCalibration::computeTrackPose() {
  cv::Mat base2targetQ = cv::Mat::eye(4, 4, CV_64F);
  cv::Mat camera2trackQ = cv::Mat::eye(4, 4, CV_64F);
  calibMat4x42Mat(minResult.cameraTrack.camera2track, camera2trackQ);
  calibMat4x42Mat(minResult.cameraTrack.base2target, base2targetQ);
  cv::Point2f totalOffset = {};
  int32_t count = 0;
  for (const auto& clip : videoClips) {
    for (int32_t i = clip.startIndex; i < clip.startIndex + clip.count; i++) {
      CalibFramePose& framePose = framePoses[i];
      cv::Mat track2base(4, 4, CV_64FC1);
      calibMat2Mat4x4(trackPoses[i].track2baseR,
                      trackPoses[i].track2baseT * minResult.cameraTrack.scale, track2base);
      framePose.cam2Target = base2targetQ * track2base * camera2trackQ;
      cv::Mat target2camera = calibHomogeneousInverse(framePose.cam2Target);
      projectPoints->updateCameraPose(target2camera);
      projectPoints->updatePoints(framePose.points);
      framePose.offset = projectPoints->projectOffset(framePose.corners);
      totalOffset += framePose.offset;
      count++;
    }
  }
  totalOffset = totalOffset / (float)count;
  minResult.offset = totalOffset.x;
}

bool CalibVideoCalibration::getFrameOffset(int32_t frameIndex, FrameOffset& innerOffsetOut,
                                           FrameOffset* trackOffset) {
  if (frameIndex < 0 || frameIndex >= (int32_t)framePoses.size()) {
    return false;
  }
  CalibFramePose& framePose = framePoses[frameIndex];
  if (!framePose.bFind || framePose.target2camR.empty() || framePose.target2camT.empty()) {
    return false;
  }
  calibMat2Mat4x4(calibHomogeneousInverse(framePose.target2camR, framePose.target2camT),
                  innerOffsetOut.cameraPose);
  innerOffsetOut.offset.avg = framePose.innerOffset.x;
  innerOffsetOut.offset.max = framePose.innerOffset.y;
  if (trackOffset != nullptr && !framePose.cam2Target.empty()) {
    calibMat2Mat4x4(framePose.cam2Target, trackOffset->cameraPose);
    trackOffset->offset.avg = framePose.offset.x;
    trackOffset->offset.max = framePose.offset.y;
  }
  return true;
}

bool CalibVideoCalibration::getPointCorners(int32_t frameIndex, PointCorners& pointCorners) {
  if (frameIndex < 0 || frameIndex >= (int32_t)framePoses.size()) {
    return false;
  }
  CalibFramePose& framePose = framePoses[frameIndex];
  if (!framePose.bFind) {
    return false;
  }
  pointCorners.count = (int32_t)framePose.corners.size();
  // cv::Point2f/3f 与 vec2f/vec3f 内存布局一致
  pointCorners.corners = (vec2f*)framePose.corners.data();
  pointCorners.points = (vec3f*)framePose.points.data();
  pointCorners.imageSize = imageSize;
  return true;
}

float CalibVideoCalibration::updateCameraTrack(const CameraTrack& cameraTrack) {
  minResult.cameraTrack = cameraTrack;
  computeTrackPose();
  return minResult.offset;
}

bool CalibVideoCalibration::saveBinary(const char* fileName) {
  try {
    std::ofstream ofs(fileName, std::ios::binary | std::ios::out);
    int32_t frameSize = (int32_t)framePoses.size();
    ofs.write(reinterpret_cast<const char*>(&frameSize), sizeof(int32_t));
    int32_t fsize = sizeof(float);
    for (int32_t i = 0; i < frameSize; i++) {
      bool bFind = framePoses[i].bFind;
      ofs.write(reinterpret_cast<const char*>(&bFind), sizeof(bool));
      if (bFind) {
        ofs.write(reinterpret_cast<const char*>(&framePoses[i].blur), fsize);
        ofs.write(reinterpret_cast<const char*>(&framePoses[i].visibility), fsize);
        int32_t pointSize = (int32_t)framePoses[i].corners.size();
        ofs.write(reinterpret_cast<const char*>(&pointSize), sizeof(int32_t));
        ofs.write(reinterpret_cast<const char*>(framePoses[i].corners.data()),
                  fsize * 2 * pointSize);
        ofs.write(reinterpret_cast<const char*>(framePoses[i].points.data()),
                  fsize * 3 * pointSize);
      }
    }
    return true;
  } catch (const std::exception& ex) {
    lastError = std::string("saveBinary error: ") + ex.what();
    LOGFLF(LogLevel::warn, lastError.c_str());
  }
  return false;
}

bool CalibVideoCalibration::loadBinary(const char* fileName) {
  try {
    std::ifstream is(fileName, std::ios::binary | std::ios::in);
    if (!is.is_open()) {
      lastError = std::string("could not open path: ") + fileName;
      LOGFLF(LogLevel::warn, lastError.c_str());
      return false;
    }
    int32_t fsize = sizeof(float);
    int32_t frameSize = 0;
    is.read(reinterpret_cast<char*>(&frameSize), sizeof(int32_t));
    framePoses.resize(frameSize);
    for (int32_t i = 0; i < frameSize; i++) {
      bool bFind = false;
      is.read(reinterpret_cast<char*>(&bFind), sizeof(bool));
      framePoses[i].bFind = bFind;
      if (bFind) {
        is.read(reinterpret_cast<char*>(&framePoses[i].blur), fsize);
        is.read(reinterpret_cast<char*>(&framePoses[i].visibility), fsize);
        int32_t pointSize = 0;
        is.read(reinterpret_cast<char*>(&pointSize), sizeof(int32_t));
        framePoses[i].corners.resize(pointSize);
        framePoses[i].points.resize(pointSize);
        is.read(reinterpret_cast<char*>(framePoses[i].corners.data()), fsize * 2 * pointSize);
        is.read(reinterpret_cast<char*>(framePoses[i].points.data()), fsize * 3 * pointSize);
      }
    }
    return true;
  } catch (const std::exception& ex) {
    lastError = std::string("loadBinary error: ") + ex.what();
    LOGFLF(LogLevel::warn, lastError.c_str());
  }
  return false;
}

const char* CalibVideoCalibration::getLastError() { return lastError.c_str(); }

}  // namespace avox
