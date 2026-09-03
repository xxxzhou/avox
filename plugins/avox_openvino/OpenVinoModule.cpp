#include "OpenVinoModule.hpp"

#include "OVEngine.hpp"
#include "avox/module/AvoxManager.hpp"

namespace avox {

bool OpenVinoModule::loadModule(IOption* option) {
  (void)option;
  // 把 OVEngine 工厂注册到 AvoxManager; VkQEnhanceLayer 走 openvinoEngineHub.create("openvino") 拿实例。
  // 库已链接(plugin link openvino), 模型在 OVEngine::loadModel() 运行期加载, 失败由调用方降级 ORT。
  AvoxManager::Get().openvinoEngineHub.reg(
      "openvino", []() -> IOVEngine* { return new OVEngine(); });
  return true;
}

AVOX_REGISTER_MODULE(OpenVinoModule, avox_openvino)

}
