#pragma once

#include "avox/module/IModule.hpp"

namespace avox {
// avox_translation 插件入口: loadModule 时把 OnnxTranslator 工厂注册到 AvoxManager,
// SubtitleAsr 通过 translatorHub.create("onnx") 查表拿实例(解除核心对 avox_translation 的编译期 include 依赖)
// 注: HttpTranslator 已迁 src/avox_zlmediakit/(依赖 mk_httpclient, 内置模块内才链得到),
//     SubtitleAsr 通过 translatorHub.create("http") 查表拿实例, 工厂由 AvoxManager::init() 中 regHttpTranslator() 注册
class TranslationModule : public IModule {
 public:
  TranslationModule() { addDep("avox_onnx"); }
  bool loadModule(IOption* option) override;
};
}
