#include "TranslationHelper.hpp"

#if AVOX_ENABLE_TRANSLATION
#include "OnnxTranslator.hpp"
#endif

namespace avox {

// plugin 模式(AVOX_PLUGIN_BUILDING 定义): 这些 C 工厂不定义 —— TranslationModule 直接 new 并注册工厂到
// AvoxManager; 且 AVOX_EXPORT 在 plugin 侧为 dllimport, 定义 dllimport 函数会 C2491 错。
// src 内置模式(不定义 AVOX_PLUGIN_BUILDING): 定义并导出(AVOX_EXPORT), 供 swig/外部直接调用
// 注: HttpTranslator 已迁 src/avox_zlmediakit/(依赖 mk_httpclient, 内置模块内才链得到);
#ifndef AVOX_PLUGIN_BUILDING
BaseTranslator* createOnnxTranslator() {
#if AVOX_ENABLE_TRANSLATION
  return new OnnxTranslator();
#endif
  return nullptr;
}
#endif  // AVOX_PLUGIN_BUILDING

}
