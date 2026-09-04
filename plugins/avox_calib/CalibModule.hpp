#pragma once

#include "avox/module/IModule.hpp"

namespace avox {

// avox_calib 插件模块: 运行期探测 OpenCV 是否可用, 注册标定工厂
// (模块名 avox_calib, 工厂注册名 "opencv", 见 AVOX_REGISTER_MODULE 第二参数)
class CalibModule : public IModule {
 public:
  bool loadModule(IOption* option) override;
  void unloadModule() override;
};

}  // namespace avox
