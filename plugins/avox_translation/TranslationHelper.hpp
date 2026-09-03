#pragma once

#include "avox/subtitle/BaseTranslator.hpp"

namespace avox {

extern "C" {
// 创建 OnnxTranslator(声明 AVOX_EXPORT —— swig 经 common.i 的 %include AvoxDef.h 认识此宏;
// plugin 模式下定义被 TranslationHelper.cpp 的 AVOX_PLUGIN_BUILDING gate 掉, TranslationModule 直接 new 替代)
AVOX_EXPORT BaseTranslator* createOnnxTranslator();
}

}
