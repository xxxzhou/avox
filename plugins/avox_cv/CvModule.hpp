#pragma once

#include "avox/module/IModule.hpp"

namespace avox {
// avox_cv 插件入口: loadModule 时把 WatermarkRemoval / YoloDetector 工厂注册到 AvoxManager,
// 业务通过 watermarkRemovalHub.create("inpaint") / yoloDetectorHub.create("yolo") 查表拿实例
// (解除核心对 avox_cv 的编译期 include 依赖)。集中收纳 onnx+opencv 相关 CV 能力。
class CvModule : public IModule {
 public:
  CvModule() {
    addDep("avox_onnx");
    addDep("avox_opencv");
  }
  bool loadModule(IOption* option) override;
};
}
