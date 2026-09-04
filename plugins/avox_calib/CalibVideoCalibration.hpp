#pragma once

// 视频序列一站式标定 (移植自 aoce_opencv/VideoCalibration)
// 逐帧喂画面(+trackPose) → 连续片段分析(模糊度/可见性筛选) → 解内参 →
// 逐帧 PnP → 手眼(bScale 自动开, 组合数按数据量自适应) → 逐帧误差
// 角点分析耗时长, 支持 saveBinary/loadBinary 缓存离线重算

#include <memory>
#include <opencv2/core.hpp>
#include <string>
#include <vector>

#include "CalibCameraCalibration.hpp"  // CalibProjectPoints
#include "CalibCameraOffset.hpp"
#include "CalibHelper.hpp"
#include "CalibImagePoints.hpp"

namespace avox {

// 连续可用片段
struct CalibVideoClip {
  int32_t startIndex = 0;
  int32_t count = 0;
};

// 每帧对应的追踪器姿态 (cv 分解存储)
struct CalibTrackMat {
  cv::Mat track2baseR;
  cv::Mat track2baseT;
};

class CalibVideoCalibration : public IVideoCalibration {
 private:
  CalibImagePoints* imagePoints = nullptr;
  // 每帧识别结果 (与 trackPoses 按帧序对齐)
  std::vector<CalibFramePose> framePoses;
  std::vector<CalibTrackMat> trackPoses;
  // 可用片段
  std::vector<CalibVideoClip> videoClips;
  int32_t selectCount = 0;
  vec2d imageSize = vec2d(0.0, 0.0);
  // 内参结果
  cv::Mat cameraMatrix;
  cv::Mat distCoeffs;
  LensModel lensModel;
  double innerOffset = 0.0;
  // 内参解算最多采样帧数
  int32_t maxInnerCount = 50;
  // 手眼计算参数 (compute 时按数据量自动填充)
  OffsetParamet pamerat;
  // 最优结果
  CameraTrackOffset minResult;
  std::unique_ptr<CalibProjectPoints> projectPoints;
  std::string lastError;

 public:
  CalibVideoCalibration();
  virtual ~CalibVideoCalibration() = default;

 public:
  virtual void setImagePoints(IImagePoints* imagePoints) override;
  virtual int32_t saveImage(IImageBuffer* image) override;
  virtual void saveTrackPose(const Mat4x4d& trackPose) override;
  virtual int32_t removeFrame(int32_t frameIndex) override;
  virtual bool compute(const VideoRTParamet& videoPar, VideoResult& videoResult) override;
  virtual bool getFrameOffset(int32_t frameIndex, FrameOffset& innerOffset,
                              FrameOffset* trackOffset = nullptr) override;
  virtual bool getPointCorners(int32_t frameIndex, PointCorners& pointCorners) override;
  virtual float updateCameraTrack(const CameraTrack& cameraTrack) override;
  virtual bool saveBinary(const char* fileName) override;
  virtual bool loadBinary(const char* fileName) override;
  virtual const char* getLastError() override;

 private:
  void reset();
  // 按模糊度/可见性分析连续可用片段
  bool computeVideoClips(float minBlur, float minVisible);
  // 解内参 (采样最多 maxInnerCount 帧)
  bool computeCameraInner();
  // 逐帧 PnP 求位姿与内参级误差
  void computeCameraPose();
  // 手眼计算, delay 为帧错位 (时间对齐搜索用)
  bool computeCameraOffset(float offsetCount, int32_t delay);
  // 用最优手眼结果重算全部帧误差
  void computeTrackPose();
};

}  // namespace avox
