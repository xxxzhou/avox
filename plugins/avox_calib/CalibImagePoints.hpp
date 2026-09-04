#pragma once

// 角点三维顶点容器 + 图案识别 (移植自 aoce_opencv/ImagePoints, 适配 OpenCV 4.13
// aruco 新 API: detectMarkers 从 contrib contrib 移入主仓 objdetect, ArucoDetector 类)
// 职责: 1 维护 3D 角点 (棋盘格自动生成/Aruco 契约注入) 2 图像识别 2D 角点并配对 3D
//       3 结合内参 PnP 求标定板位姿

#include <opencv2/core.hpp>
#include <opencv2/objdetect.hpp>
#include <string>
#include <vector>

#include "CalibHelper.hpp"

namespace avox {

// 每帧由视频帧识别得到的数据 (序列标定缓存用)
struct CalibFramePose {
  // 是否识别到角点
  bool bFind = false;
  // 模糊度 (SSIM, 越高越清晰) / 可见占比
  float blur = 0.0f;
  float visibility = 0.0f;
  // 识别角点 UV 与对应 3D 点
  std::vector<cv::Point2f> corners;
  std::vector<cv::Point3f> points;
  // PnP: 标定板在摄像机坐标系下旋转/位移 (rot 为 3x3)
  cv::Mat target2camR;
  cv::Mat target2camT;
  // 手眼结果换算的相机在标定板系下位姿 (computeTrackPose 填充)
  cv::Mat cam2Target;
  // 内参级误差 (PnP 位姿重投影) / 标定后误差 (手眼位姿重投影)
  cv::Point2f innerOffset = {};
  cv::Point2f offset = {};
};

class CalibImagePoints : public IImagePoints {
 private:
  CalibCornerType cornerType = CalibCornerType::other;
  ChessboardInfo chessboardInfo;
  // aruco: 字典 + 检测器 (4.13 新 API, 字典与参数绑在一个检测器对象)
  cv::Ptr<cv::aruco::Dictionary> dictionary;
  cv::Ptr<cv::aruco::ArucoDetector> detector;
  int32_t startArucoId = 0;
  // 3D 角点 (OpenCV 系)
  std::vector<cv::Point3f> points;
  CornerMass cornerMass;
  // 避免频繁申请 (markerIds 须为 vector<int> 以匹配 detectMarkers 的 OutputArray)
  std::vector<int> markerIds;
  std::vector<std::vector<cv::Point2f>> markerCorners;
  std::vector<std::vector<cv::Point2f>> rejected;

 public:
  CalibImagePoints() = default;
  virtual ~CalibImagePoints() = default;

 public:
  virtual void setChessboardInfo(const ChessboardInfo& info) override;
  virtual void setArucoInfo(CalibArucoType arucoType, int32_t startArucoId) override;
  virtual void setPoints(int32_t count, const vec3f* points) override;
  virtual int32_t getPointCount() override;
  virtual vec3f getPoint(int32_t index) override;
  virtual CornerMass getCornerMass() override;
  virtual CalibCornerType getCornerType() override;

 public:
  // 识别角点并配对 3D 点 (内参标定用, 无需位姿)
  bool findCorners(cv::Mat& cameraImage, std::vector<cv::Point2f>& corners,
                   std::vector<cv::Point3f>& cpoints);
  // 当前 3D 点集 (标定类内部投影验证用)
  const std::vector<cv::Point3f>& getPoints() const { return points; }
  // 识别 + PnP 求标定板在摄像机下姿态 (手眼/运行时用)
  bool computeCameraPose(cv::Mat& cameraImage, std::vector<cv::Point2f>& corners,
                         std::vector<cv::Point3f>& cpoints, const cv::Mat& cameraMatrix,
                         const cv::Mat& distCoeffs, cv::Mat& rot, cv::Mat& tra,
                         bool bContinued = false);
  // 重新开始 (清 3D 点)
  void reset();

 private:
  bool computeChessboard(cv::Mat& cameraImage, std::vector<cv::Point2f>& corners,
                         std::vector<cv::Point3f>& cpoints);
  bool computeAruco(cv::Mat& cameraImage, std::vector<cv::Point2f>& corners,
                    std::vector<cv::Point3f>& arucoPoints);
};

}  // namespace avox
