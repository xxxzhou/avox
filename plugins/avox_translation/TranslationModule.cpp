#include "TranslationModule.hpp"

#include "OnnxTranslator.hpp"
#include "avox/module/AvoxManager.hpp"
#include "avox/subtitle/BaseTranslator.hpp"

namespace avox {

bool TranslationModule::loadModule(IOption* option) {
  (void)option;
  // onnx 翻译器工厂: 内部走 AvoxManager::onnxSessionHub.create("onnx") 拿 IONNXSession 实例(由 avox_onnx 注册),
  // 模型在 OnnxTranslator::load() 运行期加载, 失败由调用方降级
  AvoxManager::Get().translatorHub.reg(
      "onnx", []() -> BaseTranslator* { return new OnnxTranslator(); });
  // http 翻译器已迁 src/avox_zlmediakit/(依赖 mk_httpclient, 内置模块内才链得到),
  // 工厂由 AvoxManager::init() 中 regHttpTranslator() 注册, SubtitleAsr 查 "http" 可拿到实例
  return true;
}

AVOX_REGISTER_MODULE(TranslationModule, avox_translation)

}
