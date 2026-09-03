#pragma once

// ============================================================================
// one-shot 子代理运行器。
//
// 对齐 dsh packages/subagent 的 spawn-in-process provider ('spawn') +
// subagent-in-process-driver 的前台路径: 在父会话一次 subagent 工具调用的栈帧内,
// 用宿主的同一批设施 (toolRuntime/systemPrompt/extensionPoints/llm, 经 AgentHost 各
// getter 借出) 创建一个瞬态子会话驱动, 跑到终态, 把末条 assistant 输出折叠成结果
// 交回父会话。不经 openAgent —— 单会话宿主只管主会话, 子会话的生灭完全包在本次
// 调用里 (见 AgentHost 头注释)。
//
// 与 dsh 的刻意偏离: 无 jobs/后台路径 (avox 无任务系统), 工具恒前台等待到终态;
// continuable 模式是二期的可选扩展 (描述符的编解码已就位)。
//
// 存储: 子会话与父会话同 sessionRoot 同 projectKey (cwd 继承父头行), 独立
// session.jsonl; 头行 origin:"subagent" + parentSession=父 id + delegationDepth=
// 父深度+1。子日志预置 approval/policy{never,delegation} 与 subagent/descriptor ——
// dsh 能直接装载 (见 compose/Subagents.cpp 的落点注释)。
// ============================================================================

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "avox_agent/core/Abort.hpp"
#include "avox_agent/core/SessionTypes.hpp"

namespace avox {

class Agent;
class AgentHost;

// 委派深度超限。单独类型: 工具层要把它转成模型可见的错误结果 (而非异常逃逸),
// 测试要能按类型断言 (dsh SubagentDepthError)。
class SubagentDepthError : public std::runtime_error {
 public:
  explicit SubagentDepthError(const std::string& message)
      : std::runtime_error(message) {}
};

// one-shot 运行的终态 (dsh SubagentResult['stopReason'])。
enum class SubagentStopReason {
  Completed,
  Aborted,
  Error,
  MaxTokens,
  // 子会话 pre-step 被拒 (dsh blocked) —— 子代理拒绝了任务。
  Refusal,
};

struct SubagentRunOutcome {
  SubagentStopReason stopReason = SubagentStopReason::Error;
  // 末条非空 assistant 消息的 content; 没有装配级消息时回退为流分片 text-delta 累积。
  // 选择与 stopReason 无关 (dsh finalAssistantOutput): 取消/截断的运行也保留部分输出。
  std::vector<ContentBlock> output;
  // 子会话 id —— 审计与测试按它找 session.jsonl。
  std::string sessionId;
};

// 前台运行一个 one-shot 子代理到终态。
//
// label: 工具入参 description, 进子会话 descriptor 的持久创建标签 (可空)。
// prompt: 任务正文 (自包含 —— 子会话不继承父会话历史, 模型措辞已在工具 schema 说明)。
// signal: 父工具调用的取消信号, 取消级联到子代理 (子会话落 turn/end{aborted})。
//
// 深度超限抛 SubagentDepthError; 子会话尚未创建时父已取消抛 std::runtime_error
// (dsh: aborted before child publication) —— 二者都由工具层转成模型可见的错误结果。
// 子会话发布之后本函数不再向上抛: 子代理的失败折叠为 stopReason=Error, 父会话的
// 工具调用栈不该被子会话的异常穿透。
SubagentRunOutcome runSubagentOneShot(
    AgentHost& host, Agent& parent, const std::string& label,
    std::vector<ContentBlock> prompt, const std::shared_ptr<AbortSignal>& signal);

}
