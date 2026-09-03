#pragma once

#include "avox_agent/core/ToolTypes.hpp"

namespace avox {

// edit: 字面文本替换 (默认恰好一次, 多处命中报错并给命中次数; replace_all 全换),
// 对齐 dsh 的 edit 契约: 匹配在 LF 规范化内容上做, 写回恢复主流行尾; 独占。
ToolDefinition makeEditTool();

}
