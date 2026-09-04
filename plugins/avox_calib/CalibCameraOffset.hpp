#pragma once

// 手眼标定 (移植自 aoce_opencv/CameraOffset)
// 眼在手上: 固定标定板, 移动相机+追踪器 (钢体), N≥3 组 (图案图 + trackPose)
// 1 PnP 求每帧标定板在摄像机系下姿态 (target2camera)
// 2 AX=XB (Tsai, 可选 scale 列改进) 求摄像机相对追踪器姿态 (camera2track)
// 3 SVD 求 base2target: A(相机在标定板系)·X(标定板→追踪器系) = B(相机→追踪器系)
// 4 全排列组合数据, 选重投影误差最优的一组 (对每帧数据都不全信)

#include <memory>
#include <opencv2/core.hpp>
#include <string>
#include <vector>

#include "CalibCameraCalibration.hpp"  // CalibProjectPoints
#include "CalibHelper.hpp"
#include "CalibImagePoints.hpp"

namespace avox {

// 一帧手眼标定记录
struct CalibMatPose {
  // 追踪器姿态 (OpenCV 系)
  cv::Mat track2baseR;
  cv::Mat track2baseT;
  // 标定板在摄像机系下姿态
  cv::Mat target2camR;
  cv::Mat target2camT;
  // 识别角点 UV + 3D 点 (验证用)
  std::vector<cv::Point2f> corners;
  std::vector<cv::Point3f> points;
  // 内参级误差 / 手眼级误差
  cv::Point2f innerOffset = {};
  cv::Point2f offset = {};
};

class CalibCameraOffset : public ICameraOffset {
 private:
  vec2d imageSize = vec2d(0.0, 0.0);
  CalibImagePoints* imagePoints = nullptr;
  cv::Mat cameraMatrix;
  cv::Mat distCoeffs;
  // 全部记录帧
  std::vector<CalibMatPose> matPoses;
  // 参与计算的帧子集
  std::vector<CalibMatPose> computePoses;
  std::unique_ptr<CalibProjectPoints> drawPoints;
  // 是否有位移缩放
  bool bScale = false;
  double scale = 1.0;
  cv::Mat base2targetQ;
  cv::Mat camera2trackQ;
  cv::Point2f minOffset;
  // 计算子集索引 → matPoses 索引映射 (suggest 位掩码还原用)
  std::vector<int32_t> newIndexs;
  std::vector<int32_t> minIndexs;
  int32_t minSize = 3;
  bool bCompute = false;
  std::string lastError;
  // 避免频繁申请
  std::vector<CalibMatPose> tMatPoses;
  std::vector<cv::Mat> track2baseR;
  std::vector<cv::Mat> track2baseT;
  std::vector<cv::Mat> target2camR;
  std::vector<cv::Mat> target2camT;

 public:
  CalibCameraOffset();
  virtual ~CalibCameraOffset() = default;

 public:
  // 手眼求解: camera2track (cv 4x4) / base2target (cv 4x4) / scale
  void handEye(const std::vector<CalibMatPose>& poses, cv::Mat& camera2track,
               cv::Mat& base2target, double& scale_);
  // 用指定索引组合求解并验证, 记录最优
  void selectOne(const std::vector<int32_t>& indexs);
  // 遍历 C(N,k) 全部组合
  void selectAll(int32_t k);
  // 图像识别 + PnP 填充一帧记录
  bool getMatPose(cv::Mat& mat, CalibMatPose& matPose);
  // 已识别帧直接入栈 (视频序列标定复用 PnP 结果, 不重复识别)
  int32_t saveTrackPoseDirect(const CalibMatPose& matPose);

 public:
  virtual void setLensModel(const LensModel& lensModel, IImagePoints* imagePoints) override;
  virtual void reset() override;
  virtual int32_t saveTrackCornerImage(const Mat4x4d& trackPose, IImageBuffer* image) override;
  virtual int32_t removeFrame(int32_t frameIndex) override;
  virtual int32_t compute(const OffsetParamet& paramet, CameraTrackOffset& cameraOffset) override;
  virtual bool getFrameOffset(int32_t frameIndex, FrameOffset& trackOffset,
                              FrameOffset& innerOffset) override;
  virtual bool getPointCorners(int32_t frameIndex, PointCorners& pointCorners) override;
  virtual float updateCameraTrack(const CameraTrack& cameraTrack) override;
  virtual const char* getLastError() override;
};

}  // namespace avox
