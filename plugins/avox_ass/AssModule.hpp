#pragma once

#include "avox/module/IModule.hpp"

namespace avox {

// avox_ass 插件入口: loadModule 时把 IAssOverlay 工厂注册进 AvoxManager(key "libass"),
// 核心经 assOverlayHub.create("libass") 查表;插件缺失/未启用时查不到 → 降级为
// 不渲染内封特效字幕轨。libass 可用性在 AssOverlay::init() 运行期探测(失败≠崩)。
class AssModule : public IModule {
 public:
  bool loadModule(IOption* option) override;
};
}
