#include "OnnxModule.hpp"

#include "ONNXRuntime.hpp"
#include "avox/module/AvoxManager.hpp"

namespace avox {

bool OnnxModule::loadModule(IOption* option) {
  (void)option;
  // 把 ONNXSession 工厂注册到 AvoxManager; OnnxTranslator 走 onnxSessionHub.create("onnx") 拿实例。
  // 库已链接(plugin link onnxruntime), 模型在 ONNXSession::loadModel() 运行期加载, 失败由调用方降级。
  AvoxManager::Get().onnxSessionHub.reg(
      "onnx", []() -> IONNXSession* { return new ONNXSession(); });
  return true;
}

AVOX_REGISTER_MODULE(OnnxModule, avox_onnx)

}
