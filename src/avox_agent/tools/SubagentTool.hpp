#pragma once

#include "avox_agent/compose/AgentHost.hpp"
#include "avox_agent/core/ToolTypes.hpp"

namespace avox {

// subagent: 模型把一个自包含子任务委派给 one-shot 子代理, 对齐 dsh 的 tool-subagent
// (前台 one-shot 路径): 子会话独立日志/审批钉 never/深度受限, 末条 assistant 输出作为
// 本工具结果回灌父会话。参数与措辞逐字段对齐 dsh; 并发安全 (子会话不写父会话);
// 无独立超时预算 (委派的期限是父 turn 的取消信号)。
ToolDefinition makeSubagentTool(AgentHost& host);

}
