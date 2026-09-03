#pragma once

#include "avox/module/IModule.hpp"

namespace avox {

// avox_avatar 插件入口: loadModule 时把 Wav2ArkitFace 工厂注册到 AvoxManager,
// 业务经 audioFaceHub.create("wav2arkit") 拿实例 (PCM->ARKit52 blendshape, 驱动虚拟人口型)。
class AvatarModule : public IModule {
 public:
  bool loadModule(IOption* option) override;
};

}
