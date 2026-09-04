#pragma once

#include "avox/module/IModule.hpp"

namespace avox {

// avox_fbx 插件模块: ufbx 解析 FBX 场景 (解析用, 导出另说)
// 模块名 avox_fbx, 工厂注册名 "fbx"
class FbxModule : public IModule {
 public:
  bool loadModule(IOption* option) override;
};

}  // namespace avox
