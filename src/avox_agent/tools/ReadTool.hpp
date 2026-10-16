#pragma once

#include "avox_agent/core/ToolTypes.hpp"

namespace avox {

// read: 按行范围读大内容, 用于分段分析。对齐 dsh 的 read 契约: offset 为 1-based,
// 输出带行号前缀。
ToolDefinition makeReadTool();

}
