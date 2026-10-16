#include "CvModule.hpp"

#include "core/WatermarkRemoval.hpp"
#include "yolo/YoloDetector.hpp"
#include "avox/module/AvoxManager.hpp"
#include "avox/AvoxVision.h"

namespace avox {

bool CvModule::loadModule(IOption* option) {
  (void)option;
// 水印去除器工厂: new WatermarkRemoval() 在本 plugin 内(同一堆),
// 业务经 watermarkRemovalHub.create("inpaint") 查表拿实例; 内部 YOLODetector/LamaInpainter
  AvoxManager::Get().watermarkRemovalHub.reg(
      "inpaint", []() -> IWatermarkRemoval* { return new WatermarkRemoval(); });
  // 通用 YOLO (Ultralytics 检测/分类) 工厂: 类名/任务/输入尺寸从模型元数据读取, avox 不内置场景知识
  AvoxManager::Get().yoloDetectorHub.reg(
      "yolo", []() -> IYoloDetector* { return new YoloDetector(); });
  return true;
}

AVOX_REGISTER_MODULE(CvModule, avox_cv)

}
