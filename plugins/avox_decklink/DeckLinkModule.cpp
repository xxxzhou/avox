#include "DeckLinkModule.hpp"

#include "DeckLinkSource.hpp"
#include "avox/module/AvoxManager.hpp"
#include "avox/module/LogHelper.hpp"

namespace avox {

bool DeckLinkModule::loadModule(IOption* option) {
  (void)option;
  // 只注册懒加载工厂, 驱动探测在DeckLinkSourceMgr构造时做
  AvoxManager::Get().vDeviceMgr.regMgrObj(
      VDeviceSdk::win_decklink,
      []() -> IVideoManager* { return new DeckLinkSourceMgr(); });
  LOGFLF(LogLevel::info,
         "decklink module init, sdk:", BLACKMAGIC_DECKLINK_API_VERSION_STRING);
  return true;
}

AVOX_REGISTER_MODULE(DeckLinkModule, avox_decklink)

}  // namespace avox
