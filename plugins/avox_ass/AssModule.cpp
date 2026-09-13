#include "AssModule.hpp"

#include "AssOverlay.hpp"
#include "avox/module/AvoxManager.hpp"
// STATIC 模式(iOS/Android, AVOX_ENABLE_STATIC)下 AVOX_REGISTER_MODULE 展开
// StaticLinkModule, 需要本头文件; 动态模式展开导出符号, 无此依赖
#include "avox/module/ModuleMgr.hpp"

namespace avox {

bool AssModule::loadModule(IOption* option) {
  (void)option;
  // 工厂注册进 AvoxManager; libass 能力在 AssOverlay::init() 运行期探测
  // (未链 libass / 初始化失败 → init 返 false, 调用方降级, 不崩)。
  AvoxManager::Get().assOverlayHub.reg(
      "libass", []() -> IAssOverlay* { return new AssOverlay(); });
  return true;
}

AVOX_REGISTER_MODULE(AssModule, avox_ass)

}
