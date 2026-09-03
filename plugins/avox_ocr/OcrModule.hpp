#pragma once

#include "avox/module/IModule.hpp"

namespace avox {

// avox_ocr 插件模块: 注册 TextRecognizer 工厂到 AvoxManager
// 注: 类名用短名 OcrModule(不带 Avox 前缀, 目录 avox_ocr 已含);
//     模块名(注册/扫描/checkLoadModel)仍为 avox_ocr, 见 AVOX_REGISTER_MODULE 第二参数。
class OcrModule : public IModule {
 public:
  bool loadModule(IOption* option) override;
};

}
