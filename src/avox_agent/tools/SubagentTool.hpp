#pragma once

#include "avox_agent/compose/AgentHost.hpp"
#include "avox_agent/core/ToolTypes.hpp"

namespace avox {

// subagent: 模型把一个自包含子任务委派给 one-shot 子代理, 对齐 dsh 的 tool-subagent
// (前台 one-shot 路径): 子会话独立日志/审批钉 never/深度受限, 末条 assistant 输出作为
ToolDefinition makeSubagentTool(AgentHost& host);

}
