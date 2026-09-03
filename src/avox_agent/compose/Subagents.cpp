#include "Subagents.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <utility>

#include "AgentHost.hpp"
#include "avox/module/LogHelper.hpp"
#include "avox_agent/core/Agent.hpp"
#include "avox_agent/core/DshLayout.hpp"
#include "avox_agent/core/ReactLoopAgent.hpp"
#include "avox_agent/core/SessionPersistence.hpp"

namespace avox {

namespace {

// dsh SUBAGENT_DELEGATION_CONTEXT 原文 (child-agent.ts), 模型可见措辞逐字对齐。
// 注册为运行时上下文 (历史尾部) 而非 system 段 —— 部署的 system prompt 在父/子之间
// 保持一致; order 120 在 dsh 的 sandbox:policy (110) 与 approval:policy (115) 之后。
const char* kDelegationContextText =
    "You are a delegated subagent: your permission scope was fixed when you "
    "were started and cannot be widened from inside this session — operations "
    "that require approval are rejected automatically. When the task needs "
    "access beyond that scope, do not retry the denied operation; state the "
    "limitation in your reply so the delegating agent can handle it.";

// 建立子会话的 provider 名。dsh 的进程内 spawn provider 注册键就是 'spawn'。
const char* kSpawnProviderName = "spawn";

// dsh delegationDepthOf: 深度地板 = max(父会话头行 delegationDepth, 父 options 的
// subagentDepth)。头行是冷恢复的持久真相, options 是本次进程内组合的显式地板 ——
// 取 max 保证单调, resume 出来的子会话不会因进程重启而变浅。
int delegationDepthOf(const SessionHeader& header, const AgentOptions& options) {
  const int persisted = header.delegationDepth.value_or(0);
  return persisted > options.subagentDepth ? persisted : options.subagentDepth;
}

// 子会话 id: 沿用宿主的 "session-<秒级时间戳>" 命名; 同秒已占用时加 "-<n>" 后缀
// (父会话与同批兄弟都可能占住本秒)。zstd 档也算占用 —— 一个 id 不能同时存在明文与
// 压缩两份日志 (dsh 根目录拒绝混编码)。
std::string allocateChildSessionId(const std::string& sessionRoot,
                                   const std::optional<std::string>& cwd,
                                   const SessionId& parentId) {
  const int64_t seconds =
      std::chrono::duration_cast<std::chrono::seconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count();
  const std::string base = "session-" + std::to_string(seconds);
  for (int attempt = 1;; ++attempt) {
    const std::string candidate =
        attempt == 1 ? base : base + "-" + std::to_string(attempt);
    if (candidate == parentId.value) continue;
    const std::string path =
        dshSessionLogPath(sessionRoot, cwd, SessionId(candidate));
    if (!std::filesystem::exists(path)
        && !std::filesystem::exists(path + ".zstd")) {
      return candidate;
    }
  }
}

// dsh toStopReason: 子会话末条 turn/end 的结束原因 → one-shot 终态。
// error / interrupted / 未知 → Error (崩溃遗留的 interrupted 视作运行失败)。
SubagentStopReason toStopReason(const TurnEndReason& reason) {
  if (std::holds_alternative<TurnEndCompleted>(reason)) {
    return SubagentStopReason::Completed;
  }
  if (std::holds_alternative<TurnEndAborted>(reason)) {
    return SubagentStopReason::Aborted;
  }
  if (std::holds_alternative<TurnEndMaxTokens>(reason)) {
    return SubagentStopReason::MaxTokens;
  }
  if (std::holds_alternative<TurnEndBlocked>(reason)) {
    return SubagentStopReason::Refusal;
  }
  return SubagentStopReason::Error;
}

const char* subagentStopReasonName(SubagentStopReason reason) {
  switch (reason) {
    case SubagentStopReason::Completed: return "completed";
    case SubagentStopReason::Aborted: return "aborted";
    case SubagentStopReason::Error: return "error";
    case SubagentStopReason::MaxTokens: return "max-tokens";
    case SubagentStopReason::Refusal: return "refusal";
  }
  return "error";
}

// dsh finalAssistantOutput: 末条非空 assistant/message 的 content; 没有装配级消息
// (流中途死掉、max-tokens 截在流内) 时回退为全部 text-delta 分片的累积。
// 选择与 stopReason 无关 —— 取消/截断的运行也把已产出的部分交回父会话。
std::vector<ContentBlock> finalAssistantOutput(const Session& session) {
  const std::vector<SessionEvent>& events = session.events();
  for (auto it = events.rbegin(); it != events.rend(); ++it) {
    if (it->type != EventType::AssistantMessageEvent) continue;
    const AssistantMessageData& data = std::get<AssistantMessageData>(it->data);
    if (data.message.content.empty()) continue;
    return data.message.content;
  }
  std::string text;
  for (const SessionEvent& event : events) {
    if (event.type != EventType::AssistantChunk) continue;
    const AssistantChunkData& data = std::get<AssistantChunkData>(event.data);
    if (const auto* delta = std::get_if<StreamTextDelta>(&data.chunk)) {
      text += delta->text;
    }
  }
  if (text.empty()) return {};
  return {ContentBlock{TextBlock{std::move(text)}}};
}

}  // namespace

SubagentRunOutcome runSubagentOneShot(
    AgentHost& host, Agent& parent, const std::string& label,
    std::vector<ContentBlock> prompt, const std::shared_ptr<AbortSignal>& signal) {
  // ---- 深度上限 (dsh assertSubagentMaxDepth: 工具仍可见, 每次启动时检查) ----
  // 父头行/选项在构造后不可变, 父驱动线程内直读是安全的。
  const SessionHeader& parentHeader = parent.session().getHeader();
  const int childDepth = delegationDepthOf(parentHeader, parent.options()) + 1;
  const int maxDepth = host.config().subagent.maxDepth;
  if (childDepth > maxDepth) {
    throw SubagentDepthError("subagent depth " + std::to_string(childDepth)
                             + " exceeds maxDepth " + std::to_string(maxDepth));
  }
  if (host.config().sessionRoot.empty()) {
    throw std::runtime_error("必须配置 sessionRoot 才能派生子会话");
  }
  // ---- 预取消 (dsh: aborted before child publication) ----
  if (signal != nullptr && signal->aborted()) {
    throw std::runtime_error(
        "subagent request was aborted before child publication");
  }

  // ---- 子会话身份: 同 root 同 projectKey (cwd 继承父头行), 独立 session.jsonl ----
  // 父会话无 cwd -> 子会话也无 cwd (落到 _no-cwd), 不替子会话发明进程 cwd。
  const std::optional<std::string> cwd = parentHeader.cwd;
  const std::string childId =
      allocateChildSessionId(host.config().sessionRoot, cwd, parentHeader.id);
  const std::string childPath = dshSessionLogPath(
      host.config().sessionRoot, cwd, SessionId(childId));

  // ---- 创建窗口 (发布之前): 子会话 + 委派审批覆盖 ----
  // approval/policy{never,delegation} 落在任何 turn 之前 —— 子会话首个审批询问就被
  // 钉死 (foldPolicy 折叠最后一条), 无需运行期补追。
  SessionHeader childHeader;
  childHeader.version = SESSION_FORMAT_VERSION;
  childHeader.id = SessionId(childId);
  childHeader.createdAt =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count();
  childHeader.cwd = cwd;
  childHeader.parentSession = parentHeader.id;
  childHeader.delegationDepth = childDepth;
  childHeader.origin = "subagent";
  auto session = std::make_unique<Session>(SessionId(childId),
                                           std::vector<SessionEvent>{},
                                           std::move(childHeader));
  session->append(ApprovalPolicyData{ApprovalPolicy::Never, std::string("delegation")});

  // ---- 子驱动: 与父同批设施, 独立会话/日志/驱动线程/作用域 ----
  // 声明顺序即异常路径的拆除顺序 (逆序析构): 级联 → 钩子 → 上下文 → child → writer。
  std::unique_ptr<SessionWriter> writer;
  std::unique_ptr<ReactLoopAgent> child;
  ScopedRegistration delegationContext;
  ScopedRegistration descriptorHook;
  ScopedRegistration abortCascade;
  {
    ReactLoopAgent::Deps deps;
    deps.systemPrompt = &host.prompt();
    deps.tools = &host.tools();
    deps.llm = host.llmProvider();
    deps.points = &host.points();
    deps.maxParallelToolCalls = host.config().maxParallelToolCalls;

    AgentOptions childOptions = parent.options();
    childOptions.subagentDepth = childDepth;

    child = std::make_unique<ReactLoopAgent>(std::move(session),
                                             std::move(childOptions), deps);
  }

  // 委派上下文 (dsh order 120): 注册在 child 自己的作用域上, 随本运行撤销。
  delegationContext = ScopedRegistration(host.prompt().context(
      PromptContext{"subagent:delegation", 120, [](const AssembleContext&) {
                      return std::string(kDelegationContextText);
                    }},
      child->scope()));

  // 描述符落点: dsh 的 provider 在子会话**首个 turn 内、首次请求之前**追加 —— 用
  // pre-step 钩子实现 (钩子在 turn/start 之后、step/start 之前跑, 且在子驱动线程、
  // 子锁外, withSession 安全)。折叠方只认首条, 但保持 turn 包围是 dsh 的既定形状。
  SubagentDescriptorData descriptor;
  descriptor.mode = SubagentMode::OneShot;
  descriptor.provider = kSpawnProviderName;
  if (!label.empty()) descriptor.label = label;
  auto descriptorAppended = std::make_shared<std::atomic<bool>>(false);
  descriptorHook = ScopedRegistration(host.points().preStep.on(
      [childPtr = child.get(), descriptorAppended, descriptor](
          PreStepPayload&,
          const Chain<PreStepPayload, PreStepDecision>::Next& next)
          -> PreStepDecision {
        if (!descriptorAppended->exchange(true)) {
          childPtr->withSession([descriptor](Session& target) {
            target.append(descriptor);
          });
        }
        return next();
      },
      child->scope()));

  // ---- 发布: 落盘对齐 + created 通知 (镜像 AgentHost::openAgent 的顺序) ----
  std::filesystem::create_directories(
      std::filesystem::path(childPath).parent_path());
  writer = std::make_unique<SessionWriter>();
  if (!writer->attach(child->session(), childPath)) {
    LOGFLF(LogLevel::warn, "[subagent] 子会话日志无法落盘: ",
           writer->lastError().c_str());
  }
  AgentLifecyclePayload createdPayload;
  createdPayload.agent = child.get();
  host.points().created.emit(createdPayload, child->scope());
  LOGFLF(LogLevel::info, "[subagent] 派生子会话 ", childId.c_str(), " (深度 ",
         std::to_string(childDepth).c_str(), "): ",
         label.empty() ? "(无标签)" : label.c_str());

  // ---- 取消级联: 父 signal → 子 cancel(CancelByParent) ----
  // onAbort 对已取消的 signal 立即同步回调 (覆盖注册竞态)。回调运行在调用父 cancel
  // 的线程上且持有父锁 —— 它只做置位与 child->cancel (拿子锁, 无反向取锁路径)。
  auto cancelled = std::make_shared<std::atomic<bool>>(false);
  if (signal != nullptr) {
    abortCascade = ScopedRegistration(signal->onAbort(
        [cancelled, childPtr = child.get()]() {
          cancelled->store(true);
          childPtr->cancel(AgentCancelCause{CancelByParent{}});
        }));
  }

  // 收尾: 按依赖逆序拆干净 (幂等); 折叠在拆除之前完成 —— 会话归 child 所有。
  bool tornDown = false;
  auto teardown = [&]() {
    if (tornDown) return;
    tornDown = true;
    abortCascade.reset();
    descriptorHook.reset();
    delegationContext.reset();
    child->shutdown();
    AgentLifecyclePayload disposedPayload;
    disposedPayload.agent = child.get();
    host.points().disposed.emit(disposedPayload, child->scope());
    writer->detach();
  };

  SubagentRunOutcome outcome;
  outcome.sessionId = childId;
  try {
    // ---- 驱动到终态: 单条任务消息起一个 turn, 无限等静止 ----
    // 本函数运行在父驱动的工具调用栈帧里 (父工具 dispatch 在父锁外), 阻塞在这里
    // 不会挡住父会话的其余交互。
    UserMessage task;
    task.id = MessageId(childId + "/delegation");
    task.content = std::move(prompt);
    task.source = userSource();
    child->followup(std::move(task));
    // 交接缝关门: cancel 对空闲 agent 是空操作 —— 若父取消落在「注册级联 → 驱动
    // 起跑」之间, 那次 cancel 只清了 inbox 没能 latch。followup 后复检一次: 已
    // abort 则补一刀 (RunningPhase 已立则生效; 尚未起跑则 inbox 已空, 子会话只会
    // 开一个不花模型调用的空 turn 立即收尾 —— 日志仍然自洽)。
    if (signal != nullptr && signal->aborted()) {
      cancelled->store(true);
      child->cancel(AgentCancelCause{CancelByParent{}});
    }
    child->whenIdle(-1);

    // ---- 折叠: 末条 turn/end → 终态; cancelled 且非干净完成 → Aborted (dsh 规则) ----
    SubagentStopReason recorded = SubagentStopReason::Error;
    const std::vector<SessionEvent>& events = child->session().events();
    for (auto it = events.rbegin(); it != events.rend(); ++it) {
      if (it->type != EventType::TurnEnd) continue;
      recorded = toStopReason(std::get<TurnEndData>(it->data).reason);
      break;
    }
    outcome.stopReason =
        (cancelled->load() && recorded != SubagentStopReason::Completed)
            ? SubagentStopReason::Aborted
            : recorded;
    outcome.output = finalAssistantOutput(child->session());
  } catch (...) {
    teardown();
    throw;
  }
  teardown();
  LOGFLF(LogLevel::info, "[subagent] 子会话 ", childId.c_str(), " 终态: ",
         subagentStopReasonName(outcome.stopReason));
  return outcome;
}

}
