#pragma once

#include "avox/module/IModule.hpp"

namespace avox {
// avox_tempo 插件入口: loadModule 时把变速不变调处理器工厂注册到
// AvoxManager.audioTempoHub ("soundtouch" = SoundTouch WSOLA)。
// ARenderTask 在 speed!=1 时 create 接入播放管线, 未装本插件核心降级变调重采样。
class TempoModule : public IModule {
 public:
  bool loadModule(IOption* option) override;
};
}
