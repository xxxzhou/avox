#pragma once

#ifdef AVOX_ENABLE_G2O
// g2o 图优化器 (移植自 aoce_g2o HandEyeOptimizer/MeshBuildOptimizer):
// 1 CalibHandEyeOptimizer — 手眼再优化: 同时优化 camera2track/base2target/
//   每帧target2camera/scale (st_handeye_graph 思路, 每帧位姿作为优化变量吸收
//   追踪器噪声; scale 顶点天然解决追踪器位移缩放; 对初值不敏感)
// 2 CalibCalibrationOptimizer — 内参BA: 内参+畸变+每帧位姿联合优化,
//   非平面(弧形幕墙)标定物上替代 calibrateCamera (其初值估计对非平面失效)

#include <memory>
#include <string>
#include <vector>

#include "CalibG2OTypes.hpp"

namespace avox {

// 手眼位姿边: 四顶点(camera2track, base2target, target2camera, scale),
// 6 维李群误差 = 实测track2base(缩放后) 与 优化量反推的 base2track 之差
class CalibHandEyeEdge : public g2o::BaseMultiEdge<6, Eigen::Isometry3d> {
 public:
  CalibHandEyeEdge() { resize(4); }

  void computeError() override;
  bool read(std::istream& is) override { return false; }
  bool write(std::ostream& os) const override { return false; }
};

class CalibHandEyeOptimizer : public ICameraTrackOptimizer {
 private:
  std::vector<TrackCorners> trackCorners;
  std::string lastError;

 public:
  CalibHandEyeOptimizer() = default;
  virtual ~CalibHandEyeOptimizer() = default;

 public:
  virtual void fillData(int32_t count, const TrackCorners* dataPtr) override;
  virtual CameraTrack compute(const HandEyeParamet& paramet, const LensModel& lensModel,
                              const CameraTrack& cameraTrack) override;
  virtual const char* getLastError() override;
};

class CalibCalibrationOptimizer : public ICalibrationOptimizer {
 private:
  std::vector<TrackCorners> trackCorners;
  LensModel resultLens;
  std::string lastError;

 public:
  CalibCalibrationOptimizer() = default;
  virtual ~CalibCalibrationOptimizer() = default;

 public:
  virtual void fillData(int32_t count, const TrackCorners* dataPtr) override;
  virtual LensModel compute(const LensModel& lensModel) override;
  virtual bool getCameraPose(int32_t frameIndex, Mat4x4d& cameraPose) override;
  virtual float getOffset(int32_t frameIndex) override;
  virtual const char* getLastError() override;
};

}  // namespace avox

#endif  // AVOX_ENABLE_G2O
