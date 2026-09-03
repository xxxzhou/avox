#pragma once

#include "avox_agent/core/ToolTypes.hpp"

namespace avox {

// write: 整体写入文件 (新建或覆盖), 对齐 dsh 的 write 契约: 返回 <path>/<type>/<content>
// 信封 + Created|Updated file; 独占 (dsh 未声明并发安全)。
ToolDefinition makeWriteTool();

}
