#pragma once

#include "avox/module/IModule.hpp"

namespace avox {
// avox_onnx 插件入口: loadModule 时把 ONNXSession 工厂注册到 AvoxManager,
// OnnxTranslator 等通过 AvoxManager::onnxSessionHub.create("onnx") 查表拿实例(解除编译期 include 依赖)
class OnnxModule : public IModule {
 public:
  bool loadModule(IOption* option) override;
};
}
