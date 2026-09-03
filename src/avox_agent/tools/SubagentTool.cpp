#include "SubagentTool.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ToolArgs.hpp"
#include "avox/module/Json.hpp"
#include "avox/module/LogHelper.hpp"
#include "avox_agent/compose/Subagents.hpp"

namespace avox {

namespace {

// dsh providerWording(false) 的全新子代理措辞 (子会话不继承父会话历史) + 前台路径
// 说明, 逐字对齐 —— 模型可见文本是 dsh 互通契约的一部分, 不做本地化。
constexpr const char* kDescription =
    "Delegate a self-contained task to a subagent (a separate agent that works "
    "in its own context) to offload focused, independent work — research, a "
    "scoped implementation, an analysis — so it does not consume this "
    "conversation's context. The subagent returns its result, not its "
    "intermediate steps. Give it a complete, standalone prompt: it does not "
    "see this conversation. This call waits for the subagent and returns its "
    "result.";

constexpr const char* kParameters = R"json({
    "type": "object",
    "properties": {
      "description": {
        "type": "string",
        "description": "A short (3-5 word) description of the delegated task, for display.",
      },
      "prompt": {
        "type": "string",
        "description": "The complete, self-contained task for the subagent. It does not share this conversation's context, so include everything it needs."
      }
    },
    "required": ["description", "prompt"]
  })json";

// dsh stopReasonError: 非 completed 终态的失败标题。
std::optional<std::string> stopReasonError(SubagentStopReason reason) {
  switch (reason) {
    case SubagentStopReason::Completed:
      return std::nullopt;
    case SubagentStopReason::Aborted:
      return std::string("subagent run was cancelled");
    case SubagentStopReason::Error:
      return std::string("subagent run failed");
    case SubagentStopReason::MaxTokens:
      return std::string("subagent run hit its token limit before finishing");
    case SubagentStopReason::Refusal:
      return std::string("subagent declined the task");
  }
  // 合并可扩展词汇: 未知终态按失败报告, 而不是把部分输出当成功 (dsh 同款)。
  return std::string("subagent run ended abnormally");
}

std::string joinTextBlocks(const std::vector<ContentBlock>& blocks) {
  std::string text;
  for (const ContentBlock& block : blocks) {
    if (const auto* textBlock = std::get_if<TextBlock>(&block)) {
      text += textBlock->text;
    }
  }
  return text;
}

// dsh withPartialText: 把子代理保留的部分输出接在失败标题后 —— 截断/取消的运行,
// 它真正说过的话仍然要到达父模型。
std::string withPartialText(const std::string& error,
                            const std::vector<ContentBlock>& output) {
  const std::string text = joinTextBlocks(output);
  if (text.empty()) return error;
  return error + "\nPartial output before the run ended:\n" + text;
}

const char* stopReasonCode(SubagentStopReason reason) {
  switch (reason) {
    case SubagentStopReason::Aborted: return "SUBAGENT_ABORTED";
    case SubagentStopReason::MaxTokens: return "SUBAGENT_MAX_TOKENS";
    case SubagentStopReason::Refusal: return "SUBAGENT_REFUSED";
    case SubagentStopReason::Completed:
    case SubagentStopReason::Error:
      break;
  }
  return "SUBAGENT_FAILED";
}

ToolResult executeSubagent(AgentHost& host, const ToolExecution& exec) {
  LOGFLF(LogLevel::info, "agent tool subagent");
  // dsh: 无属主 agent 就没有委派的所有权归属。
  if (exec.agent == nullptr) {
    return toolError(ToolOutcome::Fatal,
                     "subagent tool requires a calling agent "
                     "(exec.agent was undefined)",
                     TOOL_CODE_INVALID_ARGS);
  }
  const Json args = parseToolArgs(exec.argumentsJson);
  if (!args.bObject()) {
    return toolError(ToolOutcome::Fatal,
                     "invalid arguments: expected an object with "
                     "`description` and `prompt` strings",
                     TOOL_CODE_INVALID_ARGS);
  }
  const std::string label = toolArgString(args, "description");
  if (!args.find("prompt") || !args["prompt"].bString()) {
    return toolError(ToolOutcome::Fatal,
                     "invalid prompt: expected a string", TOOL_CODE_INVALID_ARGS);
  }
  const std::string prompt = args["prompt"].get<std::string>();
  if (prompt.empty()) {
    return toolError(ToolOutcome::Fatal,
                     "invalid prompt: must be a non-empty string",
                     TOOL_CODE_INVALID_ARGS);
  }

  SubagentRunOutcome outcome;
  try {
    outcome = runSubagentOneShot(host, *exec.agent, label,
                                 {ContentBlock{TextBlock{prompt}}}, exec.signal);
  } catch (const SubagentDepthError& e) {
    // 深度超限是模型可见的拒绝 (工具仍注册 —— 运行期策略拥有拒绝权, dsh 同款)。
    return toolError(ToolOutcome::Fatal, e.what(), "SUBAGENT_DEPTH");
  } catch (const std::exception& e) {
    // 发布前的取消/存储失败等: 子会话尚未存在, 直接以错误结果回灌。
    return toolError(ToolOutcome::Fatal, e.what(), "SUBAGENT_FAILED");
  }

  if (const std::optional<std::string> error = stopReasonError(outcome.stopReason)) {
    return toolError(ToolOutcome::Fatal,
                     withPartialText(*error, outcome.output),
                     stopReasonCode(outcome.stopReason));
  }
  // 干净完成: 子代理的输出块原样成为本工具的模型可见内容 (dsh 前台路径不包 JSON 壳,
  // runId 只进展示层 —— 这里同理, childSession 进 meta 供回放)。
  ToolResult result;
  result.outcome = ToolOutcome::Ok;
  result.content = std::move(outcome.output);
  Json meta(Json::JsonObject{});
  meta["childSession"] = outcome.sessionId;
  result.meta = meta.dump();
  return result;
}

}  // namespace

ToolDefinition makeSubagentTool(AgentHost& host) {
  ToolDefinition definition;
  definition.name = "subagent";
  definition.description = kDescription;
  definition.parametersJson = kParameters;
  definition.execute = [&host](const ToolExecution& exec) {
    return executeSubagent(host, exec);
  };
  // 委派的期限是父 turn 的取消信号 (级联到子代理), 不设独立墙钟预算。
  definition.timeoutMs = 0;
  // dsh isConcurrencySafe: 子会话不写父会话; 父会话唯一的写 (tool/call+tool/result)
  // 由父驱动串行完成。
  definition.executionMode = [](const std::string&) { return ExecutionMode::Parallel; };
  return definition;
}

}
