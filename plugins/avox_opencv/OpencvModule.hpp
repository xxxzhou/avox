#pragma once

#include "avox/module/IModule.hpp"

namespace avox {

// avox_opencv 插件模块: 运行期探测 OpenCV 是否可用
// 注: 类名用短名 OpencvModule(不带 Avox 前缀, 目录 avox_opencv 已含);
//     模块名(注册/扫描/checkLoadModel)仍为 avox_opencv, 见 AVOX_REGISTER_MODULE 第二参数。
class OpencvModule : public IModule {
 public:
  bool loadModule(IOption* option) override;
};

}
