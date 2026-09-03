#pragma once

#include "avox/module/IModule.hpp"

namespace avox {
// avox_sherpa 插件入口: loadModule 时把 SherpaAudioStt 工厂注册到 AvoxManager,
// SubtitleAsr 通过 AvoxManager::audioSttHub.create("sherpa") 查表拿实例(解除编译期 include 依赖)
class SherpaModule : public IModule {
 public:
  bool loadModule(IOption* option) override;
};
}
