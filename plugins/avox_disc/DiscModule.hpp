#pragma once

#include "avox/module/IModule.hpp"

namespace avox {
// avox_disc 插件入口: loadModule 时把 IoPlan::disc 数据源工厂注册到
// AvoxManager.ioSources, 业务 open("xx.iso") 自动分道即播(或显式 setIoPlan)。
// 另注册 IRemoteSource("disc"): 标题+时长平列表, resolve 写 disc.title。
class DiscModule : public IModule {
 public:
  bool loadModule(IOption* option) override;
};
}
