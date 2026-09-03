#include "OpencvModule.hpp"

#include "TemplateMatcher.hpp"
#include "FeatureMatcher.hpp"
#include "ColorDetector.hpp"
#include "OrientationDetector.hpp"
#include "MapMatcher.hpp"
#include "MaskBuilder.hpp"
#include "ImageProc.hpp"
#include "avox/AvoxVision.h"
#include "avox/module/AvoxManager.hpp"

#include <opencv2/opencv.hpp>

namespace avox {

bool OpencvModule::loadModule(IOption* option) {
  (void)option;
  // 探测: OpenCV 真的链上 + 可调用(getBuildInformation 返回非空构建信息)
  // 能编进 dll + 运行期拿到非空字符串 = opencv_world4xx.dll 加载成功
  std::string info = cv::getBuildInformation();
  if (info.empty()) return false;
  // 模板匹配工厂: new TemplateMatcher() 在本 plugin 内(同一堆);
  // 业务经 templateMatcherHub.create("opencv") 查表拿实例
  // (解除核心对 avox_opencv 的编译期 include 依赖, OpenCV 缺失时 create 返回 nullptr 降级)
  AvoxManager::Get().templateMatcherHub.reg(
      "opencv", []() -> ITemplateMatcher* { return new TemplateMatcher(); });
  AvoxManager::Get().featureMatcherHub.reg(
      "opencv", []() -> IFeatureMatcher* { return new FeatureMatcher(); });
  AvoxManager::Get().colorDetectorHub.reg(
      "opencv", []() -> IColorDetector* { return new ColorDetector(); });
  AvoxManager::Get().orientationDetectorHub.reg(
      "opencv", []() -> IOrientationDetector* { return new OrientationDetector(); });
  AvoxManager::Get().mapMatcherHub.reg(
      "opencv", []() -> IMapMatcher* { return new MapMatcher(); });
  AvoxManager::Get().maskBuilderHub.reg(
      "opencv", []() -> IMaskBuilder* { return new MaskBuilder(); });
  // 图像处理(load/resize 等): ImageIO 经 imageProcHub.create("opencv") 取实例,
  // 核心不链 opencv; plugin 没装时 ImageIO 降级(stb+手写双线性)
  AvoxManager::Get().imageProcHub.reg(
      "opencv", []() -> IImageProc* { return new ImageProc(); });
  return true;
}

AVOX_REGISTER_MODULE(OpencvModule, avox_opencv)

}
