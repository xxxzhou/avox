#pragma once

#include "avox/module/IModule.hpp"

namespace avox {
// avox_webrtc 插件入口: loadModule 时把 RtcPlayer/AudioProcess/RawSource 工厂注册到 AvoxManager,
// 核心层通过 AvoxManager::rtcPlayerHub.create("webrtc") / audioProcessHub.create("webrtc") 查表拿实例
class WebrtcModule : public IModule {
 public:
  bool loadModule(IOption* option) override;
};
}
