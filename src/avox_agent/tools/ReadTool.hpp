#pragma once

#include "avox_agent/core/ToolTypes.hpp"

namespace avox {

// read: 按行范围读大内容, 用于分段分析。对齐 dsh 的 read 契约: offset 为 1-based,
// 输出带行号前缀。
//
// 自带部署参数与渲染意图: 只读所以可并行, 30s 预算, 卡片按 Read 分类并把 path 放进
// locations (UI 可跳到文件)。
ToolDefinition makeReadTool();

}