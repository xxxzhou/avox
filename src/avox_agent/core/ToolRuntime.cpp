#include "ToolRuntime.hpp"

#include <algorithm>
#include <stdexcept>
#include <unordered_set>
#include <utility>

#include "Agent.hpp"
#include "avox/module/Json.hpp"
#include "avox/module/LogHelper.hpp"
#include "avox/module/Utf8.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

namespace avox {

namespace {

// 一次调用所属的作用域 (无 agent 的直接调用/测试走全局层)。
ScopeKey scopeOf(const ToolExecution& exec) {
  return exec.agent == nullptr ? nullptr : exec.agent->scope();
}

ToolResult abortedBeforeDispatch() {
  return toolError(ToolOutcome::AbortedBeforeDispatch, "tool call aborted before dispatch",
                   TOOL_CODE_ABORTED_BEFORE_DISPATCH);
}

// 取消覆盖一个已经产出的成功结果。保留它带来的追加上下文 —— 那些副作用已经发生了。
ToolResult abortedAfterBody(ToolResult prior) {
  ToolResult result = toolError(ToolOutcome::Aborted, "tool call aborted", TOOL_CODE_ABORTED);
  result.additionalContexts = std::move(prior.additionalContexts);
  return result;
}

#ifdef _WIN32

// MSVC 下 C++ 异常也是一种 SEH 异常 (code 0xE06D7363)。必须放它继续传播给外层的 catch,
// 否则一个工具抛出的 std::runtime_error 会被误报成「崩溃」, 而那两件事的处置完全不同。
constexpr DWORD kCppExceptionCode = 0xE06D7363;

int sehFilter(DWORD code) {
  return code == kCppExceptionCode ? EXCEPTION_CONTINUE_SEARCH
                                   : EXCEPTION_EXECUTE_HANDLER;
}

// 单独一层不含 __try 的调用: 含 __try 的函数不能有需要展开的对象 (MSVC C2712), 而
// ToolResult 含 string/vector 就算需展开。这里返回裸指针, 由调用方立刻接管。
ToolResult* invokeToolBody(const ToolDefinition& definition,
                           const ToolExecution& exec) {
  return new ToolResult(definition.execute(exec));
}

DWORD sehInvokeToolBody(const ToolDefinition& definition,
                        const ToolExecution& exec, ToolResult*& out) {
  __try {
    out = invokeToolBody(definition, exec);
    return 0;
  } __except (sehFilter(GetExceptionCode())) {
    return GetExceptionCode();
  }
}

#endif  // _WIN32

// 跑一次工具体, 把崩溃与异常都变成模型可见的结果对象。
ToolResult invokeGuarded(const ToolDefinition& definition,
                         const ToolExecution& exec) {
  try {
#ifdef _WIN32
    ToolResult* raw = nullptr;
    const DWORD sehCode = sehInvokeToolBody(definition, exec, raw);
    if (sehCode != 0) {
      return toolError(ToolOutcome::Crashed,
                       "工具 \"" + exec.name + "\" 崩溃 (SEH 异常码 "
                           + std::to_string(static_cast<int>(sehCode))
                           + "), 已隔离不拖垮进程",
                       TOOL_CODE_CRASHED);
    }
    std::unique_ptr<ToolResult> owned(raw);
    return owned == nullptr ? toolOk("") : std::move(*owned);
#else
    return definition.execute(exec);
#endif
  } catch (const AbortError&) {
    // 工具用抛异常的方式响应取消: 归一化成规范取消结果, 而不是当成失败。
    return toolError(ToolOutcome::Aborted, "tool call aborted", TOOL_CODE_ABORTED);
  } catch (const std::exception& e) {
    return toolError(ToolOutcome::Fatal, e.what(), TOOL_CODE_INVALID_ARGS);
  } catch (...) {
    return toolError(ToolOutcome::Fatal, "工具抛出未知异常", TOOL_CODE_CRASHED);
  }
}

}  // namespace

// ===========================================================================
// ToolLayer
// ===========================================================================

ToolLayer::ToolLayer(ScopeKey scope)
    : tools([scope](const std::string& name) {
        return scope == nullptr
                   ? "工具 \"" + name
                         + "\" 已注册 (需要 per-agent 变体请注册到该 agent 的作用域)"
                   : "工具 \"" + name + "\" 已在本作用域注册";
      }) {}

bool ToolLayer::admits(const std::string& name) const {
  bool allowed = true;
  restrictions.forEach([&](const ToolRestriction& restriction) {
    if (!allowed) return;
    if (restriction.allow.has_value()
        && std::find(restriction.allow->begin(), restriction.allow->end(), name)
               == restriction.allow->end()) {
      allowed = false;
      return;
    }
    if (restriction.deny.has_value()
        && std::find(restriction.deny->begin(), restriction.deny->end(), name)
               != restriction.deny->end()) {
      allowed = false;
    }
  });
  return allowed;
}

std::optional<std::string> ToolLayer::guardReason(const ToolExecution& exec) const {
  std::optional<std::string> reason;
  guards.forEach([&](const ToolGuard& guard) {
    if (reason.has_value() || guard == nullptr) return;
    reason = guard(exec);
  });
  return reason;
}

// ===========================================================================
// 注册
// ===========================================================================

ToolRuntime::ToolRuntime()
    : layers([this]() { change.emit(ToolsChangePayload{}); }) {}

Disposer ToolRuntime::define(ToolDefinition definition, ScopeKey owner) {
  if (definition.name.empty()) throw std::runtime_error("工具必须有名字");
  if (definition.execute == nullptr) {
    throw std::runtime_error("工具 \"" + definition.name + "\" 缺少 execute");
  }
  // parametersJson 必须是一个 JSON 对象字面量 —— 它要原样嵌进发给模型的 schema 数组,
  // 一处非法会让整个请求体坏掉, 而那时的报错离真正的原因已经很远了。
  //
  // 只做形状检查而**不用 parserJson 验证**: 本项目的解析器对空容器 ("{}" / "[]") 会抛,
  // 而一个没有属性的参数对象是完全合法的 schema。真正的语义合法性由后端判定, 这里只挡住
  // 明显的错误 (空串、忘了写、写成了数组)。
  const std::string& schema = definition.parametersJson;
  const size_t firstNonSpace = schema.find_first_not_of(" \t\r\n");
  if (firstNonSpace == std::string::npos || schema[firstNonSpace] != '{') {
    throw std::runtime_error("工具 \"" + definition.name
                             + "\" 的 parametersJson 必须是 JSON 对象字面量");
  }
  const std::string name = definition.name;
  return layers.effect(owner, [&](ToolLayer& layer) {
    return layer.tools.insert(name, std::move(definition));
  });
}

Disposer ToolRuntime::restrict(ToolRestriction restriction, ScopeKey owner) {
  return layers.effect(owner, [&](ToolLayer& layer) {
    return layer.restrictions.insert(std::move(restriction));
  });
}

Disposer ToolRuntime::addGuard(ToolGuard guard, ScopeKey owner) {
  if (guard == nullptr) throw std::runtime_error("守卫不能为空");
  return layers.effect(owner, [&](ToolLayer& layer) {
    return layer.guards.insert(std::move(guard));
  });
}

// ===========================================================================
// 视图
// ===========================================================================

const ToolDefinition* ToolRuntime::find(const std::string& name,
                                        ScopeKey scope) const {
  // 限制先行: 全局层与链上任一层不放行就不可见。
  if (!layers.global().admits(name)) return nullptr;
  for (const ToolLayer* layer : layers.chainLayers(scope)) {
    if (!layer->admits(name)) return nullptr;
  }

  // 最近的作用域层优先 (scopeChainOf 最近在前)。
  for (ScopeKey key : scopeChainOf(scope)) {
    const ToolLayer* layer = layers.peek(key);
    if (layer == nullptr) continue;
    if (const ToolDefinition* definition = layer->tools.find(name)) {
      return definition;
    }
  }
  return layers.global().tools.find(name);
}

std::vector<const ToolDefinition*> ToolRuntime::visible(ScopeKey scope) const {
  std::vector<std::string> names;
  std::unordered_set<std::string> seen;
  const auto collect = [&](const NamedEntries<ToolDefinition>& table) {
    for (const auto& entry : table.entries()) {
      if (seen.insert(entry.first).second) names.push_back(entry.first);
    }
  };
  collect(layers.global().tools);
  for (const ToolLayer* layer : layers.chainLayers(scope)) collect(layer->tools);

  // 字典序而非注册序: 见 schemasJson 的说明 (KV cache 前缀稳定性)。
  std::sort(names.begin(), names.end());

  std::vector<const ToolDefinition*> result;
  result.reserve(names.size());
  for (const std::string& name : names) {
    // 经 find 取: 它同时处理遮蔽与限制。
    if (const ToolDefinition* definition = find(name, scope)) {
      result.push_back(definition);
    }
  }
  return result;
}

std::string ToolRuntime::schemasJson(ScopeKey scope) const {
  Json array(Json::JsonArray{});
  for (const ToolDefinition* definition : visible(scope)) {
    Json function(Json::JsonObject{});
    function["name"] = definition->name;
    function["description"] = definition->description;
    function["parameters"] = parserJson(definition->parametersJson.c_str());

    Json entry(Json::JsonObject{});
    entry["type"] = "function";
    entry["function"] = std::move(function);
    array.push_back(std::move(entry));
  }
  return array.dump();
}

ExecutionMode ToolRuntime::executionMode(const ToolExecution& exec) const {
  const ToolDefinition* definition = find(exec.name, scopeOf(exec));
  if (definition == nullptr || definition->executionMode == nullptr) {
    return ExecutionMode::Exclusive;
  }
  try {
    return definition->executionMode(exec.argumentsJson);
  } catch (...) {
    // 分类器抛异常一律按独占处理: 并发是优化, 独占是安全默认。
    return ExecutionMode::Exclusive;
  }
}

// ===========================================================================
// 三阶段
// ===========================================================================

bool ToolRuntime::cancelled(const ToolExecution& exec) {
  return exec.signal != nullptr && exec.signal->aborted();
}

std::optional<std::string> ToolRuntime::guardReason(
    const ToolExecution& exec) const {
  if (std::optional<std::string> reason = layers.global().guardReason(exec)) {
    return reason;
  }
  for (const ToolLayer* layer : layers.chainLayers(scopeOf(exec))) {
    if (std::optional<std::string> reason = layer->guardReason(exec)) {
      return reason;
    }
  }
  return std::nullopt;
}

PreToolDecision ToolRuntime::serviceAsk(const ToolExecution& exec,
                                        const PreToolAsk& ask) {
  if (approval == nullptr) {
    // 没有应答方 = fail closed。
    return PreToolDeny{"需要审批但本部署没有配置审批应答方: " + ask.reason};
  }
  ApprovalOutcome outcome = ApprovalOutcome::Unavailable;
  try {
    outcome = approval->ask(exec, ask.reason);
  } catch (const std::exception& e) {
    LOGFLF(LogLevel::warn, "[tools] 审批应答方抛出异常: ", e.what());
    outcome = ApprovalOutcome::Unavailable;
  } catch (...) {
    outcome = ApprovalOutcome::Unavailable;
  }
  // **只有一次性授权才继续**, 其余一切 (拒绝、取消、不可用) 都变成拒绝。
  switch (outcome) {
    case ApprovalOutcome::AllowedOnce:
      return PreToolAllow{};
    case ApprovalOutcome::Rejected:
      return PreToolDeny{"用户拒绝了这次操作"};
    case ApprovalOutcome::Cancelled:
      return PreToolDeny{"审批被取消"};
    case ApprovalOutcome::Unavailable:
    default:
      return PreToolDeny{"审批不可用, 已按拒绝处理"};
  }
}

Prepared ToolRuntime::prepare(ToolExecution& exec) {
  const ScopeKey scope = scopeOf(exec);
  if (find(exec.name, scope) == nullptr) {
    return PreparedFinalResult{toolError(ToolOutcome::Fatal,
                                        "unknown tool: \"" + exec.name + "\"",
                                        TOOL_CODE_UNKNOWN_TOOL)};
  }
  if (cancelled(exec)) return PreparedFinalResult{abortedBeforeDispatch()};

  PreToolDecision gate = PreToolAllow{};
  try {
    PreToolPayload payload{&exec};
    gate = preExecute.run(payload, scope,
                          []() -> PreToolDecision { return PreToolAllow{}; });
  } catch (const std::exception& e) {
    // 准入链失败按拒绝处理: 一次不完整的准入决策不能当作许可。
    return PreparedPostResult{toolError(
        ToolOutcome::Denied, std::string("准入策略失败: ") + e.what(), TOOL_CODE_DENIED)};
  }

  if (const auto* ask = std::get_if<PreToolAsk>(&gate)) {
    gate = serviceAsk(exec, *ask);
  }

  std::optional<std::string> denial;
  if (const auto* deny = std::get_if<PreToolDeny>(&gate)) {
    denial = deny->reason;
  } else {
    // 单调守卫在全部可协商策略之后求值。
    denial = guardReason(exec);
  }
  if (denial.has_value()) {
    // 走 PostResult 而不是 FinalResult: 被拒绝的调用也要经过 post-execute ——
    // 模型反复敲一个被拒的调用正是循环卫生最该打断的情形。
    return PreparedPostResult{
        toolError(ToolOutcome::Denied, *denial, TOOL_CODE_DENIED)};
  }

  if (cancelled(exec)) return PreparedPostResult{abortedBeforeDispatch()};
  return PreparedDispatch{};
}

ToolResult ToolRuntime::dispatch(ToolExecution& exec) {
  const ScopeKey scope = scopeOf(exec);
  const ToolDefinition* definition = find(exec.name, scope);
  if (definition == nullptr) {
    return toolError(ToolOutcome::Fatal, "unknown tool: \"" + exec.name + "\"",
                     TOOL_CODE_UNKNOWN_TOOL);
  }

  // around 包装器可以替换 exec.signal, 所以先留住调用方那个。
  const std::shared_ptr<AbortSignal> callerSignal = exec.signal;
  bool bodyInvoked = false;

  ToolResult result;
  try {
    AroundToolPayload payload{&exec};
    result = aroundExecute.run(payload, scope, [&]() -> ToolResult {
      // 恢复 signal 的 RAII: 免得 post-execute 监听器看到一个已被超时 abort 的信号,
      // 那会让它们把一次正常结果误判成取消。
      const std::shared_ptr<AbortSignal> wrapperSignal = exec.signal;
      struct SignalGuard {
        ToolExecution& exec;
        std::shared_ptr<AbortSignal> restore;
        ~SignalGuard() { exec.signal = restore; }
      } guard{exec, wrapperSignal};

      // 把调用方信号重新 fuse 回来: 于是 around 包装器替换 signal 不可能切断调用方的取消。
      FusedAbort fused(callerSignal, wrapperSignal);
      exec.signal = fused.signal();
      if (exec.signal->aborted()) return abortedBeforeDispatch();

      bodyInvoked = true;
      ToolResult body = invokeGuarded(*definition, exec);
      // 取消**不抛弃**已启动的工具体: 上面已经等它返回了, 这里才把成功结论改写掉。
      if (fused.signal()->aborted() && !body.isError()) {
        return abortedAfterBody(std::move(body));
      }
      return body;
    });
  } catch (const std::exception& e) {
    return toolError(ToolOutcome::Fatal,
                     std::string("around-dispatch 失败: ") + e.what(),
                     TOOL_CODE_CRASHED);
  }

  // around 链结束后再查一次调用方取消: 包装器可能 await 过。
  if (callerSignal != nullptr && callerSignal->aborted() && !result.isError()) {
    return bodyInvoked ? abortedAfterBody(std::move(result))
                       : abortedBeforeDispatch();
  }
  return result;
}

ToolResult ToolRuntime::commit(ToolExecution& exec, ToolResult toolResult) {
  const ScopeKey scope = scopeOf(exec);

  PostToolDecision decision = PostToolAccept{};
  try {
    PostToolPayload payload{&exec, &toolResult};
    decision = postExecute.run(
        payload, scope, []() -> PostToolDecision { return PostToolAccept{}; });
  } catch (const std::exception& e) {
    // 后处理链是加工层: 它的失败不该把一次成功的调用变成失败, 也不该隐藏结果。
    LOGFLF(LogLevel::warn, "[tools] post-execute 链失败, 保留原结果: ", e.what());
    decision = PostToolAccept{};
  }

  if (const auto* block = std::get_if<PostToolBlock>(&decision)) {
    ToolResult blocked =
        toolError(ToolOutcome::Fatal, block->reason, TOOL_CODE_DENIED);
    // 阻断保留已产出的追加上下文: 那些是别的监听器的贡献, 与本次阻断无关。
    blocked.additionalContexts = std::move(toolResult.additionalContexts);
    toolResult = std::move(blocked);
  } else {
    auto& accept = std::get<PostToolAccept>(decision);
    if (accept.content.has_value()) toolResult.content = std::move(*accept.content);
    for (UserMessage& context : accept.additionalContexts) {
      toolResult.additionalContexts.push_back(std::move(context));
    }
  }

  // 编码卫生: read/grep 等按字节读本地文件, 中文 Windows 的 GBK(ANSI) 日志原始字节若
  // 进了会话, 下一次请求体就是非法 UTF-8, 服务端一律 400 且每次必现。commit 是全部
  // 工具结果的唯一出口, 在这单点洗净即覆盖所有工具 (含被 post-execute 改写/阻断的路径)。
  for (ContentBlock& block : toolResult.content) {
    if (auto* text = std::get_if<TextBlock>(&block)) {
      text->text = ensureUtf8(text->text);
    } else if (auto* reasoning = std::get_if<ReasoningBlock>(&block)) {
      reasoning->text = ensureUtf8(reasoning->text);
    }
  }
  for (UserMessage& context : toolResult.additionalContexts) {
    for (ContentBlock& block : context.content) {
      if (auto* text = std::get_if<TextBlock>(&block)) {
        text->text = ensureUtf8(text->text);
      }
    }
  }
  // meta 是序列化 JSON: 只能修补不能整体重编码, 否则会把结构一起转坏。
  if (toolResult.meta.has_value()) {
    toolResult.meta = patchInvalidUtf8(*toolResult.meta);
  }

  ToolResultPayload payload{&exec, &toolResult};
  result.emit(payload, scope);
  return toolResult;
}

}
