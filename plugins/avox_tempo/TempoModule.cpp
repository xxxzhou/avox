#include "TempoModule.hpp"

#include "SoundTouchTempo.hpp"
#include "avox/module/AvoxManager.hpp"
// 静态模式(iOS/WASM)下 AVOX_REGISTER_MODULE 展开 StaticLinkModule, 需此头
#include "avox/module/ModuleMgr.hpp"

namespace avox {

bool TempoModule::loadModule(IOption* option) {
  (void)option;
  // 变速不变调处理器注册: IAudioTempo("soundtouch"), ARenderTask 变速帧时 create
  // 注: 注册项无显式卸载接口(RegeditObj), 与插件DLL句柄常驻策略配套(进程内不FreeLibrary)
  AvoxManager::Get().audioTempoHub.reg(
      "soundtouch", []() -> IAudioTempo* { return new SoundTouchTempo(); });
  return true;
}

AVOX_REGISTER_MODULE(TempoModule, avox_tempo)

}
