#include "CalibModule.hpp"

#include <opencv2/opencv.hpp>

#include "CalibCameraCalibration.hpp"
#include "CalibCameraOffset.hpp"
#include "CalibImagePoints.hpp"
#include "CalibPnpCameraPose.hpp"
#include "CalibVideoCalibration.hpp"
#include "CalibWallMeshBuild.hpp"
#include "avox/module/AvoxManager.hpp"

#ifdef AVOX_ENABLE_G2O
#include "CalibG2OOptimizers.hpp"
#include "g2o/core/factory.h"
#include "g2o/core/optimization_algorithm_factory.h"
#include "g2o/core/hyper_graph_action.h"
#endif

namespace avox {

bool CalibModule::loadModule(IOption* option) {
  (void)option;
  // 探测: opencv_world dll 真的加载成功 (可编进 dll 且运行期拿到非空构建信息)
  std::string info = cv::getBuildInformation();
  if (info.empty()) return false;
  // 虚拟制片标定工厂 (算法移植自 aoce 虚拟制片标定, 见 doc/plan/虚拟制片标定移植方案.md)
  AvoxManager::Get().imagePointsHub.reg(
      "opencv", []() -> IImagePoints* { return new CalibImagePoints(); });
  AvoxManager::Get().cameraCalibrationHub.reg(
      "opencv", []() -> ICameraCalibration* { return new CalibCameraCalibration(); });
  AvoxManager::Get().cameraOffsetHub.reg(
      "opencv", []() -> ICameraOffset* { return new CalibCameraOffset(); });
  AvoxManager::Get().pnpCameraPoseHub.reg(
      "opencv", []() -> IPnpCameraPose* { return new CalibPnpCameraPose(); });
  AvoxManager::Get().videoCalibrationHub.reg(
      "opencv", []() -> IVideoCalibration* { return new CalibVideoCalibration(); });
  AvoxManager::Get().ledMeshBuildHub.reg(
      "opencv", []() -> ILedMeshBuild* { return new CalibWallMeshBuild(); });
#ifdef AVOX_ENABLE_G2O
  // 图优化路径 (M2): 弧形幕墙内参 BA / 手眼再优化
  AvoxManager::Get().cameraTrackOptimizerHub.reg(
      "g2o", []() -> ICameraTrackOptimizer* { return new CalibHandEyeOptimizer(); });
  AvoxManager::Get().calibrationOptimizerHub.reg(
      "g2o", []() -> ICalibrationOptimizer* { return new CalibCalibrationOptimizer(); });
#endif
  return true;
}

void CalibModule::unloadModule() {
#ifdef AVOX_ENABLE_G2O
  // g2o 单例必须在插件卸载前销毁, 否则释放图资源时崩 (aoce 血泪经验)
  g2o::Factory::destroy();
  g2o::OptimizationAlgorithmFactory::destroy();
  g2o::HyperGraphActionLibrary::destroy();
#endif
}

AVOX_REGISTER_MODULE(CalibModule, avox_calib)

}  // namespace avox
