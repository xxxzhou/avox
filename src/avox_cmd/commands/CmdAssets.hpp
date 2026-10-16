#pragma once

#include "avox_cmd/CmdRegistry.hpp"

namespace avox {

// assets 子命令: 管理插件运行时资源 (AI 模型 + 运行时 DLL)。
// C++ 直接读 assets/script/assets_manifest.json + 扫描 plugins/ 比对缺失;
Command cmdAssets();

}
