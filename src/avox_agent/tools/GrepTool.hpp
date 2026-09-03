#pragma once

#include "avox_agent/core/ToolTypes.hpp"

namespace avox {

// grep: 按正则搜文件行, 返回匹配行号 + 窗口片段, 按文件分组。对齐 dsh 的 grep 契约:
// pattern 始终按正则处理。
//
// 与 read 互补: 大日志里定位特定行用它更快, 拿到行号后再用 read 看上下文。
// 只读所以可并行; 结果卡片按 Search 分类。
ToolDefinition makeGrepTool();

}