#pragma once

#include <string>
#include <vector>

#include "avox/AvoxDef.h"

namespace avox {

// 内容片段类型。
//
// 原来定义在 AgentExport.h; 那个头随旧 IAgentClient 一并删除后, 这里是它唯一的消费者,
// 所以定义就落在这里。
enum class PartType { Text, ImageUrl };

// Provider 协议层语义结构 (不含任何厂商 wire 格式)。
// ProviderRequest/Message/Part 是协议无关的对话状态, 由 LlmProviderAdapter 从会话投影
// (core 的 LlmRequest/Message) 逐次组装; 各 Provider 实现负责把它序列化成本家 wire 格式
// (OpenAI/Anthropic/Responses 各异)。
// 故此处绝不出现 "role":"user" / "tool_calls" / "choices" 等 wire 字段。

// 对话角色 (比公共 AgentRole 多一个 Tool: round-2 工具结果消息)
enum class ProviderRole { System, User, Assistant, Tool };

// 内容片段 (复用 AgentExport.h 的 PartType)
struct ProviderPart {
  PartType type = PartType::Text;
  std::string data;
};

// 单条对话消息 (语义)
struct ProviderMessage {
  ProviderRole role = ProviderRole::User;
  std::vector<ProviderPart> parts;
  // round-2+ assistant 消息: 预序列化的 tool_calls wire (Provider 已清洗: 去 index、补 call_<i> id)。
  // 适配层不解析, 原样经 Provider 塞回 wire。仅 role==Assistant 且本轮模型回了 tool_calls 时填。
  std::string assistantToolCallsWire;
  // 仅 role==Assistant: 本轮推理块拼接的 CoT 回传。dsh (#2786 后) 的规则是每个带推理
  // 的轮都送 reasoning_content —— 官方思维链模式在 tool-call 轮必填, 其余轮后端忽略;
  // 网关把会话转码给别家时靠哈希这段文本恢复该轮的思维链签名。
  std::string assistantReasoning;
  // role==Tool 时关联的 tool_call_id (与上一条 assistant.tool_calls[].id 一一对应, R4)
  std::string toolCallId;
};

// 一次推理请求 (跨 round 累积, 替代旧 wire round-trip: parserJson payload 反解)
struct ProviderRequest {
  std::vector<ProviderMessage> messages;
  std::string model;
  float temperature = 0.7f;
  int maxTokens = 16384;
  // 推理等级: 'low' / 'medium' / 'high' (OpenAI reasoning_effort 兼容), 空 = 不发。
  //
  // 默认值在 LlmProviderAdapter 层物化 (默认 "medium"): 切换路由或轮换模型时由
  // adapterDefaults.reasoningEffort 标记摘除重算, 用户显式设的值跨路由保留。
  std::string reasoningEffort;
  // 原生工具定义 (OpenAI FC JSON; Anthropic/Responses 将来在自身 Provider 内转格式)
  std::string toolsDefinitionJson;
  // 系统提示词: Agent 注入 (SkillRegistry::systemPrompt), Provider 置 messages 开头
  std::string systemPrompt;
  // 注: 触发词路由(routing)由 Agent 在 build*Request 时一次性追加到 user.data (applyRouting),
  // 不经 Provider —— routing 是 avox skill 业务而非 LLM 协议, 且保证 round-2 继承时 wire 字节不变。
};

// SSE 事件 (Provider 把厂商 SSE 行解析成统一事件序列, Agent 据此驱动预算/展示/工具累积)
enum class SseEventKind {
  Text,           // 正文 token
  Reasoning,      // 推理 token (仅展示, 不计预算进展, R3)
  ToolCallBegin,  // tool_call 片: 带 index/id/type/name (部分可能空, 非空才有效)
  ToolCallArgs,   // tool_call arguments 分片
  Done,
  Error,
};

struct SseEvent {
  SseEventKind kind = SseEventKind::Text;
  std::string text;       // Text/Reasoning 内容
  int toolIndex = -1;     // ToolCall* 关联 index (Agent 按 index 升序累积分片, R1)
  std::string toolId;     // ToolCallBegin: tool_call id
  std::string toolType;   // ToolCallBegin: 类型 (默认 function)
  std::string toolName;   // ToolCallBegin: 函数名 (非空且变化时 Agent 流式展示)
  std::string argsFrag;   // ToolCallArgs: arguments 分片
  std::string errorMsg;   // Error
};

// 协议 Provider 抽象: "协议相关"(payload 构建 + SSE 解析 + 工具消息格式)收口于此;
// "协议无关"(HTTP/中断/工具执行/日志)留适配层与 core。
// 第一版仅 ChatProvider; Anthropic/Responses 预留 (factory fallback, 暂不实装)。
class IAgentProvider {
 public:
  virtual ~IAgentProvider() = default;
  virtual const char* name() = 0;   // 协议标识 (openai-chat/anthropic/openai-responses)
  // 首轮+round-2 共用: 把语义 ProviderRequest 序列化成本家 wire payload
  virtual std::string buildPayload(const ProviderRequest& req) = 0;
  // 解析一行已剥 "data: " 的 SSE JSON → 统一事件 (可能多条, 如同片 tool_call 的 Begin+Args)
  virtual bool onSseLine(const std::string& dataLine, std::vector<SseEvent>& out) = 0;
  // 流结束判定 ([DONE]/message_stop/response.completed 各协议不同)
  virtual bool isStreamEnd(const std::string& dataLine) = 0;
};

}
