#pragma once

// ============================================================================
// 工具的定义、执行标识与决策词汇表。
//
// 对齐 dsh 的 packages/core/tools/src/index.ts 的类型部分。
//
// 一条贯穿全局的原则: **失败与拒绝走结果对象, 不走异常**。deny、超时、取消、崩溃全部变成
// 一个模型可见的 ToolResult, 模型看得到、能自愈; 异常只留给真正的编程错误。
//
// 与旧 IToolResult 的关键差异: 成败不再靠 "FAIL:" 字符串前缀判定 (工具正常输出恰好以它
// 开头就误判, 而且 SEH 崩溃、参数错误、被审批拒绝、超时对上层完全同形), 改由 outcome
// 枚举 + code 承载。
// ============================================================================

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "Abort.hpp"
#include "SessionTypes.hpp"
#include "ToolPresentation.hpp"

namespace avox {

class Agent;

// ---------------------------------------------------------------------------
// 结果
// ---------------------------------------------------------------------------

// 一次工具调用的结局。
enum class ToolOutcome {
  Ok,
  // 被策略链或守卫拒绝 (审批、沙箱、plan mode)。
  Denied,
  // 超时预算耗尽。
  Timeout,
  // 取消, 且**工具体已经启动过**。
  Aborted,
  // 取消, 且工具体未曾启动。
  AbortedBeforeDispatch,
  // 工具体崩溃 (Windows 下由 SEH 隔住的硬件异常)。
  Crashed,
  // 工具自身报告的失败 (参数非法、外部命令返回错误)。
  Fatal,
};

// 规范失败码。放在 code 字段里供策略与诊断路由 —— 与模型可见文本分离。
inline constexpr const char* TOOL_CODE_ABORTED = "ABORTED";
// 与 ABORTED 分开的理由: 工具体是否已经跑过, 决定副作用是否可能已经发生。重试策略、
// 沙箱策略, 以及「要不要提示用户刚才那个点击可能已经生效」全依赖这个区分。
inline constexpr const char* TOOL_CODE_ABORTED_BEFORE_DISPATCH =
    "ABORTED_BEFORE_DISPATCH";
inline constexpr const char* TOOL_CODE_TIMEOUT = "TOOL_TIMEOUT";
inline constexpr const char* TOOL_CODE_UNKNOWN_TOOL = "UNKNOWN_TOOL";
inline constexpr const char* TOOL_CODE_DENIED = "DENIED";
inline constexpr const char* TOOL_CODE_CRASHED = "CRASHED";
inline constexpr const char* TOOL_CODE_INVALID_ARGS = "INVALID_ARGS";

struct ToolResult {
  ToolOutcome outcome = ToolOutcome::Ok;
  // 模型可见内容。
  std::vector<ContentBlock> content;
  // 内部路由码 (成功时为空)。
  std::string code;
  // 工具私有的展示载荷 (JSON 文本), 持久化进 tool/result 事件供回放重现卡片。
  std::optional<std::string> meta;
  // 追加进 next-step inbox 的上下文 (循环提醒、文件变更通知)。
  std::vector<UserMessage> additionalContexts;
  // 工具声明本轮到此为止 (「反向控制」: 提前结束工具循环也是数据说话)。
  bool concludesTurn = false;

  bool isError() const { return outcome != ToolOutcome::Ok; }
};

// 构造一条纯文本成功结果。
inline ToolResult toolOk(std::string text) {
  ToolResult result;
  result.outcome = ToolOutcome::Ok;
  result.content.push_back(TextBlock{std::move(text)});
  return result;
}

// 构造一条失败结果。模型看到的文本统一带 "Error: " 前缀 —— 与 dsh 一致, 让模型对失败的
// 识别不依赖各工具的措辞。
inline ToolResult toolError(ToolOutcome outcome, std::string message,
                           std::string code) {
  ToolResult result;
  result.outcome = outcome;
  result.content.push_back(TextBlock{"Error: " + message});
  result.code = std::move(code);
  return result;
}

// ---------------------------------------------------------------------------
// 执行标识
// ---------------------------------------------------------------------------

// 一次调用能否与兄弟调用重叠。
//
// 默认 Exclusive 是安全默认: 调度器**不做跨调用的冲突分析**, 安全性完全依赖工具自己
// 老实声明。GUI 自动化必须永远 Exclusive —— 焦点是全局独占资源。
enum class ExecutionMode { Exclusive, Parallel };

// 一次工具调用的标识与上下文。
struct ToolExecution {
  CallId callId;
  std::string name;
  // 模型原样产出的 arguments JSON 字符串 (未解析)。
  //
  // 只传原文不传解析结果: tool/call 事件要记的就是原文, 而各工具对入参的校验规则不同,
  // 由它自己 parse 一次成本可忽略, 换来的是 core 的头文件不必依赖 JSON 类型。
  std::string argumentsJson;
  // 代表哪个 agent 执行 (由驱动填)。
  Agent* agent = nullptr;
  // 调用方拥有的取消信号。around 包装器可以替换它 (超时), 但注册表会在工具体之前把
  // 原始调用方信号重新 fuse 回来 —— 于是替换不可能切断调用方的取消。
  std::shared_ptr<AbortSignal> signal;
};

// ---------------------------------------------------------------------------
// 定义
// ---------------------------------------------------------------------------

struct ToolDefinition {
  std::string name;
  std::string description;
  // 入参的 JSON Schema (OpenAI function calling 的 parameters 字段)。
  std::string parametersJson;

  // 执行一次已获准的调用。
  //
  // 异步工作必须观察或转发 exec.signal, 并且只在自己拥有的工作到达静止后才返回 ——
  // 注册表不会抛弃已启动的调用 (同进程代码杀不掉, 抛弃它会留下写了一半的文件或残留
  // 子进程), 但它也无法硬杀。
  std::function<ToolResult(const ToolExecution&)> execute;

  // 协作式超时预算 (毫秒); 0 表示无期限。
  //
  // 由 TimeoutPolicy 执行。**永不发给模型** —— schema 序列化只白名单
  // name/description/parameters。声明它等于断言本工具会把 signal 转发给一个能到达静止
  // 的协作式实现。
  int timeoutMs = 0;

  // 并发分类 (纯函数, 不填即 Exclusive)。
  std::function<ExecutionMode(const std::string& argumentsJson)> executionMode;

  // UI 渲染意图 (纯函数, 见 ToolPresentation.hpp 的契约)。不填则退化为通用呈现。
  std::function<std::optional<ToolCallView>(const std::string& argumentsJson)>
      presentCall;
  std::function<std::optional<ToolResultView>(const std::string& argumentsJson,
                                              const ToolResult& result)>
      presentResult;
};

// ---------------------------------------------------------------------------
// 决策
// ---------------------------------------------------------------------------

// tools/pre-execute: 派发前的准入决策。
struct PreToolAllow {};
struct PreToolDeny {
  std::string reason;
};
// 需要人工确认。**没有审批应答方时退化为 deny** (fail closed)。
struct PreToolAsk {
  std::string reason;
};
using PreToolDecision = std::variant<PreToolAllow, PreToolDeny, PreToolAsk>;

struct PreToolPayload {
  ToolExecution* exec = nullptr;
};

// tools/execute: around-dispatch。
//
// 包装器**只能改 exec->signal**, 调用身份不可变。
struct AroundToolPayload {
  ToolExecution* exec = nullptr;
};

// tools/post-execute: 结果的接受、替换、补充或阻断。
struct PostToolAccept {
  // 替换模型可见内容 (不填则保留原样)。
  std::optional<std::vector<ContentBlock>> content;
  // 追加进 next-step 的上下文。
  std::vector<UserMessage> additionalContexts;
};
// 把纠正性反馈变成一条失败结果 (例如 hook 判定这次写入违规)。
struct PostToolBlock {
  std::string reason;
};
using PostToolDecision = std::variant<PostToolAccept, PostToolBlock>;

struct PostToolPayload {
  ToolExecution* exec = nullptr;
  // 派发得到的结果 (只读; 要改就通过返回 Accept 携带替换内容)。
  const ToolResult* result = nullptr;
};

// tools/result: 最终结果的观察 (只读)。
struct ToolResultPayload {
  const ToolExecution* exec = nullptr;
  const ToolResult* result = nullptr;
};

// 在全部 pre-execute 监听器之后、工具体之前求值的**单调**守卫。
//
// 返回原因即拒绝, 返回 nullopt 表示不干预。因为守卫**没有 allow 结果**, 监听器顺序
// 无法把一次拒绝翻回许可 —— 这是硬性禁令与可协商策略的分界。
using ToolGuard = std::function<std::optional<std::string>(const ToolExecution&)>;

// 一个作用域的工具可见性限制。allow 与 deny 都给时取交集。
struct ToolRestriction {
  std::optional<std::vector<std::string>> allow;
  std::optional<std::vector<std::string>> deny;
};

// 审批应答方。
//
// 结果词汇表里没有「永久允许」: 一次询问的答案只能是一次性授权, 持久偏好属于另一个概念
// (会话策略, 走 approval/policy 日志事件)。
class ApprovalAnswerer {
 public:
  virtual ~ApprovalAnswerer() = default;

  virtual ApprovalOutcome ask(const ToolExecution& exec,
                              const std::string& reason) = 0;
};

}
