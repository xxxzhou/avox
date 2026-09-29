#include "DiscModule.hpp"

#include "IOParseDisc.hpp"
#include "DiscSource.hpp"
#include "avox/module/AvoxManager.hpp"
// 静态模式(iOS/WASM)下 AVOX_REGISTER_MODULE 展开 StaticLinkModule, 需此头
#include "avox/module/ModuleMgr.hpp"

namespace avox {

bool DiscModule::loadModule(IOption* option) {
  (void)option;
  IoPlanDesc desc = {};
  desc.name = "disc(bluray iso/bdmv streaming)";
  AvoxManager::Get().ioSources.regInitFunc(
      IoPlan::disc, desc,
      []() -> AVSource* { return new IOParseDisc(); });
  // 远程内容源工厂: IRemoteSource("disc") 标题列表+选标题
  AvoxManager::Get().remoteSourceHub.reg(
      "disc", []() -> IRemoteSource* { return new DiscSource(); });
  return true;
}

AVOX_REGISTER_MODULE(DiscModule, avox_disc)

}
