#pragma once

#include "avox_agent/core/ToolTypes.hpp"

namespace avox {

// grep: 按正则搜文件行, 返回匹配行号 + 窗口片段, 按文件分组。对齐 dsh 的 grep 契约:
// pattern 始终按正则处理。
ToolDefinition makeGrepTool();

}
