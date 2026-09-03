#pragma once

#include "avox_cmd/CmdRegistry.hpp"

namespace avox {

// assets 子命令: 管理插件运行时资源 (AI 模型 + 运行时 DLL)。
// C++ 直接读 assets/script/assets_manifest.json + 扫描 plugins/ 比对缺失;
// 下载时调 fetch_assets.py (spawn 机器 python, stderr 直通控制台显示进度条)。
// 用法: avox_cli assets -l           # 列出所有资源及就绪状态
//       avox_cli assets -i           # 交互式多选下载
//       avox_cli assets --all        # 下载所有缺失项
//       avox_cli assets -s id1,id2   # 下载指定项
//       avox_cli assets --plugin avox_sherpa -l
Command cmdAssets();

}
