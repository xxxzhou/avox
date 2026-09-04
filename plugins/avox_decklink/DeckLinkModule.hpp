#pragma once

#include "avox/module/IModule.hpp"

namespace avox {

// DeckLink(Blackmagic)采集卡动态插件
// 注册VDeviceSdk::win_decklink设备管理器, 运行期依赖Desktop Video驱动
// (驱动未装时设备列表为空, 不影响插件加载)
class DeckLinkModule : public IModule {
 public:
  bool loadModule(IOption* option) override;
};

}  // namespace avox
