#pragma once

#include "avox/module/IModule.hpp"

namespace avox {
// avox_openvino 插件入口: loadModule 时把 OVEngine 工厂注册到 AvoxManager,
// VkQEnhanceLayer 通过 openvinoEngineHub.create("openvino") 查表拿实例
// (plugin 没装时 create 返回 nullptr, VkQEnhanceLayer 降级 ORT CPU)
class OpenVinoModule : public IModule {
 public:
  bool loadModule(IOption* option) override;
};
}
