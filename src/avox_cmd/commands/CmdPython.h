#pragma once

#include "avox_cmd/CmdRegistry.hpp"

namespace avox {

// python 子命令: spawn 机器 python 跑代码/脚本 (可用 import avox 调 avox 能力)。
// 用法: avox_cli python <脚本.py> [-i 输入]  或  avox_cli python -e <内联代码> [-i 输入]
Command cmdPython();

}
