#pragma once

#include "avox_agent/core/ToolTypes.hpp"

namespace avox {

// todo_write: 整表快照写会话的 todo 列表, 对齐 dsh 的 todo_write 契约:
// 追加 todo/write 事件 (展示层状态, 不上模型 surface), 返回三态计数摘要;
// 独占。
ToolDefinition makeTodoWriteTool();

}
