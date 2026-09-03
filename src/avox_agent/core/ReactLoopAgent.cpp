#include "ReactLoopAgent.hpp"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

#include "avox/module/LogHelper.hpp"

namespace avox {

namespace {

// 作用域退出时执行, 且**绝不让异常逃逸**。
//
// 用它写 turn/end 与 step/end: 每条退出路径 (正常、取消、异常) 都必须落下收尾标记, 而
// 析构里抛异常会直接 terminate。这是 dsh 的 finally 在 C++ 里的对应物。
template <class Fn>
class ScopeExit {
 public:
  explicit ScopeExit(Fn callback) : fn(std::move(callback)) {}

  ~ScopeExit() {
    try {
      fn();
    } catch (...) {
      // 收尾标记写失败已经无处上报 (turn 都结束了), 唯一合法的处置是不让它逃逸。
    }
  }

  ScopeExit(const ScopeExit&) = delete;
  ScopeExit& operator=(const ScopeExit&) = delete;

 private:
  Fn fn;
};

// 显式推导指引: 声明了构造函数与删除的拷贝构造之后, 聚合初始化的 CTAD 不再自动生成。
template <class Fn>
ScopeExit(Fn) -> ScopeExit<Fn>;

// 把 LlmFailure 完整带过异常边界: 只 throw runtime_error(message) 的话, turn 边界的
// catch 拿不到 code, 展示层与策略看到的都是 UNKNOWN —— HTTP 400 与 429 的处置本就不同。
class LlmFailureError : public std::runtime_error {
 public:
  explicit LlmFailureError(LlmFailure failureValue)
      : std::runtime_error(failureValue.message), failure(std::move(failureValue)) {}

  LlmFailure failure;
};

SurfaceIntent appendIntent() {
  SurfaceIntent intent;
  intent.surfaceOp = SurfaceAppend{};
  return intent;
}

SurfaceIntent appendIntent(std::vector<size_t> sources) {
  SurfaceIntent intent;
  intent.surfaceOp = SurfaceAppend{};
  intent.sourceEventSeqs = std::move(sources);
  return intent;
}

bool configEquals(const LlmCallConfig& a, const LlmCallConfig& b) {
  return a.provider == b.provider && a.model == b.model
         && a.temperature == b.temperature && a.maxTokens == b.maxTokens
         && a.reasoningEffort == b.reasoningEffort && a.stop == b.stop;
}

// header 相等 ⟺ 请求前缀字节相同 —— 这就是 KV cache 命中的判据。
//
// adapterDefaults 不参与比较: 它是「哪些值是 adapter 填的」这一元信息, 不进请求体。
bool headerEquals(const EpochHeader& a, const EpochHeader& b) {
  return configEquals(a.config, b.config) && a.system == b.system
         && a.toolsJson == b.toolsJson;
}

// 把折叠出的 header 变成下一次请求的提议: 删掉 adapter 物化的默认值。
//
// 于是切换路由后新 adapter 会重新物化自己的默认值, 而用户显式设的值跨 step、跨路由保留。
// temperature 不在摘除之列 —— dsh 的 adapterDefaults 只放 {reasoningEffort, maxTokens},
// avox 约定永远显式写 temperature (轮换模型时保留)。reasoningEffort 走同档: 物化时标记,
// 切路由前摘除重算 (DSH 同款, 见 packages/llm/llm/src/call-config.ts)。
LlmCallConfig requestProposal(const EpochHeader& header) {
  LlmCallConfig proposal = header.config;
  if (header.adapterDefaults.has_value()) {
    if (header.adapterDefaults->reasoningEffort) proposal.reasoningEffort.reset();
    if (header.adapterDefaults->maxTokens) proposal.maxTokens.reset();
  }
  return proposal;
}

}  // namespace

// ===========================================================================
// 构造与销毁
// ===========================================================================

ReactLoopAgent::ReactLoopAgent(std::unique_ptr<Session> session,
                               AgentOptions options, Deps dependencies)
    : agentOptions(std::move(options)),
      deps(dependencies),
      sessionOwned(std::move(session)) {
  if (sessionOwned == nullptr) throw std::runtime_error("agent 必须有会话");
  if (deps.systemPrompt == nullptr || deps.tools == nullptr || deps.llm == nullptr
      || deps.points == nullptr) {
    throw std::runtime_error("agent 依赖不完整 (systemPrompt/tools/llm/points)");
  }
  if (deps.maxParallelToolCalls < 1) {
    throw std::runtime_error("maxParallelToolCalls 必须是正整数");
  }

  InboxNotifications notifications;
  // inbox 的通知只作转发, 不做决策 —— 于是「谁能改队列」这个问题只有一个答案: 本 agent。
  inboxOwned = std::make_unique<Inbox>(*sessionOwned, notifications);
  runtimeContext = std::make_unique<RuntimeContextProjection>(*sessionOwned);

  // 从日志恢复最后一个 turn 号: resume 出来的会话要接着数, 而不是从 1 重新开始。
  int lastTurn = 0;
  for (const SessionEvent& event : sessionOwned->events()) {
    if (event.type != EventType::TurnStart) continue;
    lastTurn = std::get<TurnStartData>(event.data).turn;
  }
  phase = IdlePhase{lastTurn};

  driverThread = std::thread([this]() { driverMain(); });
}

ReactLoopAgent::~ReactLoopAgent() { shutdown(); }

void ReactLoopAgent::shutdown() {
  {
    std::lock_guard<std::mutex> lock(mtx);
    if (stopping) {
      // 已经停过: 但线程可能还没 join 完 (另一个调用者正在 join)。
    } else {
      stopping = true;
      // 取消当前活动。disposed 原因永不 latch, 所以驱动不会为此再开一个 turn。
      if (auto* running = std::get_if<RunningPhase>(&phase)) {
        running->abort->abort(AgentCancelCause{CancelByDisposed{}});
      } else if (auto* maintenance = std::get_if<MaintenancePhase>(&phase)) {
        maintenance->abort->abort(AgentCancelCause{CancelByDisposed{}});
      }
    }
    wakeCv.notify_all();
  }
  if (driverThread.joinable()) driverThread.join();
}

const SessionId& ReactLoopAgent::id() const { return sessionOwned->id(); }

void ReactLoopAgent::withSession(const std::function<void(Session&)>& fn) {
  if (fn == nullptr) return;
  std::lock_guard<std::mutex> lock(mtx);
  fn(*sessionOwned);
}

// ===========================================================================
// 相位
// ===========================================================================

AgentStatus ReactLoopAgent::statusOfLocked() const {
  // 维护相位对外报 Idle: 一次后台压缩不该让 UI 闪一下 running。
  return std::holds_alternative<RunningPhase>(phase) ? AgentStatus::Running
                                                     : AgentStatus::Idle;
}

AgentStatus ReactLoopAgent::status() const {
  std::lock_guard<std::mutex> lock(mtx);
  return statusOfLocked();
}

void ReactLoopAgent::setPhaseLocked(Phase next) {
  const AgentStatus previous = statusOfLocked();
  phase = std::move(next);
  const AgentStatus current = statusOfLocked();
  if (previous == current) return;
  // 只在跨越 idle/running 边界时通知。通知在锁内派发 —— 与会话观察者同一条约定:
  // 监听器不得回调进 agent 的方法。
  AgentStatusPayload payload;
  payload.agent = this;
  payload.status = current;
  deps.points->status.emit(payload, scope());
}

void ReactLoopAgent::wakeDriverLocked(bool wakeAfterAbort) {
  if (std::holds_alternative<IdlePhase>(phase)) {
    pendingWake = true;
    wakeCv.notify_one();
    return;
  }

  // 非 idle: latch 起来, 在驱动自己的收敛边界重放。
  //
  // 活驱动自己会 claim 队列, 所以只有「维护中」与「abort 之后」两种情形需要 latch。
  const std::shared_ptr<AbortController>* controller = nullptr;
  bool maintenance = false;
  if (auto* running = std::get_if<RunningPhase>(&phase)) {
    controller = &running->abort;
  } else if (auto* maint = std::get_if<MaintenancePhase>(&phase)) {
    controller = &maint->abort;
    maintenance = true;
  }
  if (controller == nullptr) return;

  const std::optional<AgentCancelCause> reason = (*controller)->signal()->reason();
  // disposed 永不 latch: 否则 teardown 会等一个永远不来的新 turn。
  if (reason.has_value() && std::holds_alternative<CancelByDisposed>(*reason)) return;

  if (!maintenance && !wakeAfterAbort) return;
  if (auto* running = std::get_if<RunningPhase>(&phase)) {
    running->wakeRequested = true;
  } else if (auto* maint = std::get_if<MaintenancePhase>(&phase)) {
    maint->wakeRequested = true;
  }
}

// ===========================================================================
// 对外操作
// ===========================================================================

void ReactLoopAgent::send(UserMessage message, InboxTarget target, bool wakeup) {
  std::lock_guard<std::mutex> lock(mtx);

  // 唤醒输入不能加入一个正在死掉的活动, 所以它改投下一个 turn。
  //
  // 分类**在插入之前**算出来: 否则一个从 splice 观察者里重入的 cancel 会把它重新分类。
  bool wakingAfterAbort = false;
  if (wakeup) {
    if (auto* running = std::get_if<RunningPhase>(&phase)) {
      wakingAfterAbort = running->abort->signal()->aborted();
    } else if (auto* maintenance = std::get_if<MaintenancePhase>(&phase)) {
      wakingAfterAbort = maintenance->abort->signal()->aborted();
    }
  }
  const InboxTarget resolved =
      wakingAfterAbort ? InboxTarget::NextTurn : target;
  inboxOwned->append(resolved, std::move(message));
  if (wakeup) wakeDriverLocked(wakingAfterAbort);
}

void ReactLoopAgent::cancel(AgentCancelCause cause, bool keepInbox) {
  std::lock_guard<std::mutex> lock(mtx);
  if (!keepInbox) {
    inboxOwned->clear();
    if (auto* running = std::get_if<RunningPhase>(&phase)) {
      running->wakeRequested = false;
    } else if (auto* maintenance = std::get_if<MaintenancePhase>(&phase)) {
      maintenance->wakeRequested = false;
    }
  }
  // 无活动时是空操作 —— 不「预约」取消后续工作, 否则一次早到的取消会杀掉用户随后发起的
  // 下一轮。
  if (auto* running = std::get_if<RunningPhase>(&phase)) {
    running->abort->abort(std::move(cause));
  } else if (auto* maintenance = std::get_if<MaintenancePhase>(&phase)) {
    maintenance->abort->abort(std::move(cause));
  }
}

bool ReactLoopAgent::whenIdle(int timeoutMs) {
  std::unique_lock<std::mutex> lock(mtx);
  const auto settled = [this]() {
    return !driverActive && std::holds_alternative<IdlePhase>(phase)
           && !pendingWake;
  };
  if (timeoutMs < 0) {
    idleCv.wait(lock, settled);
    return true;
  }
  return idleCv.wait_for(lock, std::chrono::milliseconds(timeoutMs), settled);
}

bool ReactLoopAgent::runMaintenance(
    const std::function<void(const std::shared_ptr<AbortSignal>&)>& task) {
  std::shared_ptr<AbortController> controller;
  int lastTurn = 0;
  {
    std::lock_guard<std::mutex> lock(mtx);
    // 已有驱动或另一个维护任务占用时不执行 —— 维护任务要独占 agent。
    auto* idle = std::get_if<IdlePhase>(&phase);
    if (idle == nullptr || driverActive) return false;
    lastTurn = idle->lastTurn;
    controller = std::make_shared<AbortController>();
    setPhaseLocked(MaintenancePhase{controller, lastTurn, false});
  }

  bool wakeRequested = false;
  try {
    // 锁外执行: 维护任务通常要发模型请求 (压缩要生成摘要)。
    task(controller->signal());
  } catch (const std::exception& e) {
    LOGFLF(LogLevel::warn, "[agent] 维护任务失败: ", e.what());
  } catch (...) {
    LOGFLF(LogLevel::warn, "[agent] 维护任务抛出未知异常");
  }

  {
    std::lock_guard<std::mutex> lock(mtx);
    if (auto* maintenance = std::get_if<MaintenancePhase>(&phase)) {
      wakeRequested = maintenance->wakeRequested;
      setPhaseLocked(IdlePhase{maintenance->lastTurn});
    }
    // 期间到达的唤醒输入留在 inbox 里, 现在补放。
    if (wakeRequested && inboxOwned->hasPending()) {
      pendingWake = true;
      wakeCv.notify_one();
    }
    idleCv.notify_all();
  }
  return true;
}

// ===========================================================================
// 驱动主循环
// ===========================================================================

void ReactLoopAgent::driverMain() {
  std::unique_lock<std::mutex> lock(mtx);
  for (;;) {
    wakeCv.wait(lock, [this]() {
      return stopping
             || (pendingWake && std::holds_alternative<IdlePhase>(phase));
    });
    if (stopping) return;

    pendingWake = false;
    const int lastTurn = std::get<IdlePhase>(phase).lastTurn;
    auto controller = std::make_shared<AbortController>();
    setPhaseLocked(RunningPhase{controller, lastTurn, 0, false});
    driverActive = true;

    lock.unlock();
    kick(controller);
    lock.lock();

    driverActive = false;
    idleCv.notify_all();
  }
}

void ReactLoopAgent::kick(std::shared_ptr<AbortController> controller) {
  try {
    while (runTurn(controller)) {}
  } catch (...) {
    // **驱动边界是错误的终点**: 一个 turn 的失败 (含取消) 不会杀掉 loop, 下一次唤醒
    // 照常开新 turn。失败已经在 runTurn 里落进 turn/end 并上报过了。
  }

  std::lock_guard<std::mutex> lock(mtx);
  if (auto* running = std::get_if<RunningPhase>(&phase)) {
    const int turn = running->turn;
    const bool wake = running->wakeRequested;
    setPhaseLocked(IdlePhase{turn});
    if (wake && inboxOwned->hasPending()) {
      pendingWake = true;
      wakeCv.notify_one();
    }
  }
}

bool ReactLoopAgent::runTurn(std::shared_ptr<AbortController>& controller) {
  const std::shared_ptr<AbortSignal> signal = controller->signal();
  signal->throwIfAborted();

  int turn = 0;
  {
    std::lock_guard<std::mutex> lock(mtx);
    auto* running = std::get_if<RunningPhase>(&phase);
    if (running == nullptr) throw std::runtime_error("turn 在非运行相位启动");
    turn = running->turn + 1;
    running->turn = turn;
    sessionOwned->append(TurnStartData{turn});
  }

  std::optional<TurnEndReason> ends;
  // 每条退出路径都要落 turn/end。
  const ScopeExit turnGuard{[this, turn, &ends]() {
    std::lock_guard<std::mutex> lock(mtx);
    sessionOwned->append(
        TurnEndData{turn, ends.value_or(TurnEndReason{TurnEndCompleted{}})});
  }};

  int stepsRun = 0;
  InboxTarget target = InboxTarget::NextTurn;
  try {
    for (;;) {
      signal->throwIfAborted();
      const int step = stepsRun + 1;

      PreparedStep decision = preStep(target, turn, step, signal);
      if (decision.rejected) {
        // pre-step 拒绝: turn 关闭且不花一次模型调用。被 claim 的消息就此消失 ——
        // 既不丢弃回队列也不投影成历史。
        ends = TurnEndBlocked{};
        return false;
      }
      if (ends.has_value() && decision.messages.empty()) break;
      // 一条被清掉的唤醒消息仍然占用这个 turn 边界, 但不花模型调用。
      if (stepsRun == 0 && decision.messages.empty()) {
        ends = TurnEndCompleted{};
        return false;
      }

      signal->throwIfAborted();
      {
        std::lock_guard<std::mutex> lock(mtx);
        sessionOwned->append(StepStartData{turn, step});
        if (auto* running = std::get_if<RunningPhase>(&phase)) running->step = step;
      }
      {
        const ScopeExit stepGuard{[this, turn, step]() {
          std::lock_guard<std::mutex> lock(mtx);
          sessionOwned->append(StepEndData{turn, step});
        }};
        {
          std::lock_guard<std::mutex> lock(mtx);
          for (UserMessage& message : decision.messages) {
            sessionOwned->append(UserMessageData{std::move(message)}, appendIntent());
          }
        }
        std::optional<TurnEndReason> stepEnd =
            runStep(decision.assembly, turn, step, signal);
        // max-tokens 粘性: 一旦某步撞了上限, 后面正常完成的步骤不能把 turn 结果降级。
        if (!ends.has_value()
            || !std::holds_alternative<TurnEndMaxTokens>(*ends)) {
          ends = stepEnd;
        }
      }
      stepsRun = step;
      signal->throwIfAborted();

      if (ends.has_value()) {
        bool nextStepEmpty = false;
        {
          std::lock_guard<std::mutex> lock(mtx);
          nextStepEmpty = inboxOwned->nextStep().empty();
        }
        if (nextStepEmpty) {
          TurnStoppingPayload payload;
          payload.agent = this;
          payload.turn = turn;
          payload.signal = signal;
          deps.points->turnStopping.run(payload, scope());
          signal->throwIfAborted();
        }
      }
      if (ends.has_value()) {
        std::lock_guard<std::mutex> lock(mtx);
        // **重读队列**: 反对者的唯一手段是 steer 往 inbox 里塞东西, 于是数据决定,
        // 监听器顺序不影响结果。
        if (inboxOwned->nextStep().empty()) break;
      }
      target = InboxTarget::NextStep;
    }
  } catch (const AbortError& e) {
    ends = TurnEndAborted{e.cause()};
    throw;
  } catch (const LlmFailureError& e) {
    // 失败细节原样穿到 turn 边界: 状态码/路由码不再退化成 UNKNOWN。
    ends = TurnEndError{e.failure};
    reportError(turn, stepsRun, e.what());
    throw;
  } catch (const std::exception& e) {
    if (signal->aborted()) {
      ends = TurnEndAborted{
          signal->reason().value_or(AgentCancelCause{CancelByUser{}})};
      throw;
    }
    ends = TurnEndError{LlmFailure{e.what(), "UNKNOWN"}};
    reportError(turn, stepsRun, e.what());
    throw;
  }

  std::lock_guard<std::mutex> lock(mtx);
  if (!inboxOwned->hasPending()) return false;
  // 换一个新的取消令牌: 挂在旧令牌上的 latch 由此失效 (活驱动自己会 claim 队列)。
  controller = std::make_shared<AbortController>();
  if (auto* running = std::get_if<RunningPhase>(&phase)) {
    running->abort = controller;
    running->wakeRequested = false;
    running->step = 0;
  }
  return true;
}

// ===========================================================================
// 一个 step
// ===========================================================================

ReactLoopAgent::PreparedStep ReactLoopAgent::preStep(
    InboxTarget target, int turn, int step,
    const std::shared_ptr<AbortSignal>& signal) {
  PreparedStep prepared;

  std::vector<UserMessage> claimed;
  {
    std::lock_guard<std::mutex> lock(mtx);
    claimed = inboxOwned->claim(target, turn);
  }

  AssembleContext assembleContext;
  assembleContext.agent = this;
  assembleContext.scope = scope();
  assembleContext.signal = signal;
  // 锁外装配: 段落求值可能要读磁盘 (skill 目录)。
  prepared.assembly = deps.systemPrompt->assemble(assembleContext);
  signal->throwIfAborted();

  std::vector<UserMessage> messages = std::move(claimed);

  // 动态运行时上下文走历史尾部, 且只在与上次不同时才产出 —— 前缀一个字节都不动。
  std::string contextText;
  for (const std::string& section : prepared.assembly.contextSections) {
    if (!contextText.empty()) contextText += "\n\n";
    contextText += section;
  }
  if (!contextText.empty()) {
    std::lock_guard<std::mutex> lock(mtx);
    if (std::optional<UserMessage> projected = runtimeContext->project(contextText)) {
      messages.push_back(std::move(*projected));
    }
  }

  PreStepPayload payload;
  payload.agent = this;
  payload.messages = std::move(messages);
  payload.turn = turn;
  payload.step = step;
  payload.signal = signal;

  PreStepDecision decision = deps.points->preStep.run(
      payload, scope(), [&payload]() -> PreStepDecision {
        return PreStepEnter{std::move(payload.messages)};
      });
  signal->throwIfAborted();

  if (std::holds_alternative<PreStepReject>(decision)) {
    prepared.rejected = true;
    return prepared;
  }
  prepared.messages = std::move(std::get<PreStepEnter>(decision).messages);
  return prepared;
}

std::optional<TurnEndReason> ReactLoopAgent::runStep(
    const PromptAssembly& assembly, int turn, int step,
    const std::shared_ptr<AbortSignal>& signal) {
  // 重试循环**在同一个 step 内**: turn/step 编号不变, 于是日志里能看出「同一步重试了
  // 三次」。退避策略挂在 agent/request-error 上, 驱动只认 retry / terminal 两种回答。
  for (;;) {
    signal->throwIfAborted();
    BuiltRequest built = buildRequest(turn, step, assembly, signal);

    BlockAssembler assembler;
    std::vector<size_t> chunkSeqs;
    LlmStreamHandler handler;
    handler.onChunk = [&](const StreamChunk& chunk) {
      // 分片逐条落日志 (token 级回放保真, 含 usage/finish), 同时喂给组装器。
      //
      // 这里不检查取消也不抛: provider 已经拿到 signal, 由它自己观察并返回 Aborted ——
      // 从回调里抛异常会穿过 provider 的调用栈, 那是资源泄漏的常见来源。
      std::lock_guard<std::mutex> lock(mtx);
      chunkSeqs.push_back(
          sessionOwned->append(AssistantChunkData{turn, step, chunk}));
      assembler.push(chunk);
    };

    const LlmFinish finish = deps.llm->stream(built.request, handler);

    // 取消定稿 (dsh #2134): 流消费期间被取消时, 已送达前缀收尾成 assistant/message
    // (interrupted: true), 先于 aborted 的 turn/end —— 追问与分支要包含用户已读到的
    // 内容。工具调用块从未分派, 由 interruptedBlocks 省略。失败的 attempt 永不收尾:
    // error 走下面的 waterfall, 重试窗口内落地的取消也不复活它的前缀 (本分支只认
    // provider 观察到的流内取消)。
    if (finish.kind == LlmFinishKind::Aborted && signal->aborted()) {
      std::vector<ContentBlock> prefix = assembler.interruptedBlocks();
      if (!prefix.empty()) {
        AssistantMessage message;
        message.id = MessageId(assistantMessageId(sessionOwned->id(), turn, step));
        message.source = modelSource(built.request.config.provider,
                                     built.request.config.model);
        message.content = std::move(prefix);
        std::lock_guard<std::mutex> lock(mtx);
        AssistantMessageData data;
        data.turn = turn;
        data.step = step;
        data.message = std::move(message);
        data.usage = finish.usage;
        data.interrupted = true;
        sessionOwned->append(std::move(data), appendIntent(chunkSeqs));
      }
    }
    signal->throwIfAborted();

    if (finish.kind == LlmFinishKind::Error
        || finish.kind == LlmFinishKind::Aborted) {
      RequestErrorPayload payload;
      payload.agent = this;
      payload.turn = turn;
      payload.step = step;
      payload.provider = built.request.config.provider;
      payload.failure = finish.failure.value_or(
          LlmFailure{"request aborted", "ABORTED"});
      payload.signal = signal;

      const RequestErrorAction action = deps.points->requestError.run(
          payload, scope(), []() -> RequestErrorAction { return std::nullopt; });
      signal->throwIfAborted();
      if (!action.has_value()) {
        // 没人接管 = 失败终结。抛给 turn 边界记成 error —— 带完整 LlmFailure,
        // code (BAD_REQUEST/RATE_LIMITED/...) 不在异常边界丢失。
        throw LlmFailureError(payload.failure);
      }
      continue;
    }

    AssistantMessage message;
    message.content = assembler.take();
    // 确定性 id + model source: dsh 装载校验要求 assistant 消息带非空 id 与
    // {kind:'model', provider, model} source —— provider/model 从消息顶层挪进了 source。
    message.id = MessageId(assistantMessageId(sessionOwned->id(), turn, step));
    message.source = modelSource(built.request.config.provider,
                                 built.request.config.model);
    const std::vector<ToolCallBlock> calls = [&message]() {
      std::vector<ToolCallBlock> found;
      for (const ContentBlock& block : message.content) {
        if (const auto* call = std::get_if<ToolCallBlock>(&block)) {
          found.push_back(*call);
        }
      }
      return found;
    }();

    {
      std::lock_guard<std::mutex> lock(mtx);
      AssistantMessageData data;
      data.turn = turn;
      data.step = step;
      data.message = std::move(message);
      data.usage = finish.usage;
      sessionOwned->append(std::move(data), appendIntent(chunkSeqs));
    }

    if (finish.kind == LlmFinishKind::MaxTokens) {
      return TurnEndReason{TurnEndMaxTokens{}};
    }
    if (calls.empty()) return TurnEndReason{TurnEndCompleted{}};

    const ToolCallsOutcome outcome = executeToolCalls(calls, turn, step, signal);
    if (outcome.aborted) signal->throwIfAborted();
    // nullopt = 还有工具跑过, 继续下一步; concludesTurn = 工具声明本轮到此为止。
    if (outcome.concluded) return TurnEndReason{TurnEndCompleted{}};
    return std::nullopt;
  }
}

// ===========================================================================
// 请求装配
// ===========================================================================

ReactLoopAgent::BuiltRequest ReactLoopAgent::buildRequest(
    int turn, int step, const PromptAssembly& assembly,
    const std::shared_ptr<AbortSignal>& signal) {
  LlmCallConfig seedConfig;
  {
    std::lock_guard<std::mutex> lock(mtx);
    const EpochHeader* persisted = sessionOwned->requestHeader();
    if (requestHeaderLogged && persisted != nullptr) {
      seedConfig = requestProposal(*persisted);
    } else {
      // 本实例从它声明的路由起步, 只恢复「那个确切模型上用户显式设的」采样参数。
      //
      // temperature 恒显式 (adapterDefaults wire 放不下它的物化标志): 路由相同就恢复,
      // 轮换到别的模型则用新路由的物化默认值。
      seedConfig.provider = agentOptions.provider;
      seedConfig.model = agentOptions.model;
      seedConfig.maxTokens = agentOptions.maxTokens;
      if (persisted != nullptr
          && persisted->config.provider == seedConfig.provider
          && persisted->config.model == seedConfig.model) {
        seedConfig.temperature = persisted->config.temperature;
        // reasoningEffort 与 temperature 同档: 路由相同就恢复 (用户/UI 显式设的覆盖), 切到
        // 别的模型则用新路由的物化默认值。temperature 不标记、reasoningEffort 标记是因为
        // dsh wire 形状 (adapterDefaults 只放 {reasoningEffort, maxTokens}), 但语义一致。
        seedConfig.reasoningEffort = persisted->config.reasoningEffort;
      }
    }
  }

  RequestPayload payload;
  payload.agent = this;
  payload.turn = turn;
  payload.step = step;
  payload.signal = signal;
  LlmCallConfig proposed = deps.points->request.run(
      payload, scope(), [&seedConfig]() { return seedConfig; });
  signal->throwIfAborted();

  if (proposed.provider.empty() || proposed.model.empty()) {
    throw std::runtime_error(
        "agent 没有 provider/model: 设置 AgentOptions 或经 agent/request 提供");
  }

  BuiltRequest built;
  built.prepared = deps.llm->prepareCall(proposed);
  signal->throwIfAborted();

  EpochHeader header;
  header.config = built.prepared.config;
  header.adapterDefaults = built.prepared.adapterDefaults;
  if (!assembly.system.empty()) header.system = assembly.system;
  if (!assembly.toolsJson.empty()) header.toolsJson = assembly.toolsJson;

  {
    std::lock_guard<std::mutex> lock(mtx);
    const EpochHeader* baseline = sessionOwned->requestHeader();
    if (!requestHeaderLogged) {
      // 本实例的首个请求: 日志已有 header 事件说明这是 resume。
      const RequestHeaderReason reason = baseline == nullptr
                                             ? RequestHeaderReason::Initial
                                             : RequestHeaderReason::Resume;
      sessionOwned->append(RequestHeaderData{header, reason});
      requestHeaderLogged = true;
    } else if (baseline == nullptr || !headerEquals(*baseline, header)) {
      // **只在变化时**追加。一次不动提示词的正常会话应当只有一条 header 事件 ——
      // 那是 KV cache 前缀稳定的可断言判据。
      sessionOwned->append(RequestHeaderData{header, RequestHeaderReason::Change});
    }

    RequestContext requestContext;
    requestContext.provider = built.prepared.config.provider;
    requestContext.model = built.prepared.config.model;
    requestContext.contextWindow = built.prepared.contextWindow;
    const RequestContext* previous = sessionOwned->requestContext();
    if (previous == nullptr || previous->provider != requestContext.provider
        || previous->model != requestContext.model
        || previous->contextWindow != requestContext.contextWindow) {
      sessionOwned->append(RequestContextData{requestContext});
    }

    built.request.messages = sessionOwned->deriveMessages();
  }

  built.request.config = built.prepared.config;
  built.request.system = assembly.system;
  built.request.toolsJson = assembly.toolsJson;
  built.request.signal = signal;
  return built;
}

// ===========================================================================
// 工具调用
// ===========================================================================

void ReactLoopAgent::appendSkippedToolCall(const ToolCallBlock& call, int turn,
                                           int step) {
  std::lock_guard<std::mutex> lock(mtx);
  const size_t callSeq = sessionOwned->append(
      ToolCallData{turn, step, call.id, call.name, call.arguments});

  ToolResultMessage message;
  message.id = MessageId(toolResultMessageId(sessionOwned->id(), turn, step, call.id));
  ToolResultBlock block;
  block.toolCallId = call.id;
  block.content.push_back(ToolResultContent(TextBlock{
      "Error: tool call aborted before dispatch"}));
  block.isError = true;
  message.content.push_back(std::move(block));
  message.source = toolSource(call.id);

  ToolResultData data;
  data.turn = turn;
  data.step = step;
  data.message = std::move(message);
  data.error = ToolResultError{call.name, TOOL_CODE_ABORTED_BEFORE_DISPATCH};
  sessionOwned->append(std::move(data), appendIntent({callSeq}));
}

ReactLoopAgent::ToolCallsOutcome ReactLoopAgent::executeToolCalls(
    const std::vector<ToolCallBlock>& calls, int turn, int step,
    const std::shared_ptr<AbortSignal>& signal) {
  ToolCallsOutcome outcome;

  // 一个槽位 = 一次调用的就地结果。worker 线程只写 result/needsPost/settled, driver 在
  // 提交阶段读; 二者用一把调度锁串行, 避免共享 ToolExecution 的跨线程访问。
  struct Slot {
    ToolExecution exec;
    ToolResult result;
    bool settled = false;
    bool needsPost = false;
  };

  const int maxParallel = deps.maxParallelToolCalls;
  std::mutex schedMtx;
  std::condition_variable schedCv;
  int inFlight = 0;

  // 组装一次调用的执行上下文。并发分类与准入都基于同样的字段。
  auto makeExec = [&](const ToolCallBlock& call) -> ToolExecution {
    ToolExecution exec;
    exec.callId = call.id;
    exec.name = call.name;
    exec.argumentsJson = call.arguments;
    exec.agent = this;
    exec.signal = signal;
    return exec;
  };

  // 追加 tool/call, 返回它的事件 seq 供 result 挂接。
  auto appendCall = [&](const ToolCallBlock& call) -> size_t {
    std::lock_guard<std::mutex> lock(mtx);
    return sessionOwned->append(
        ToolCallData{turn, step, call.id, call.name, call.arguments});
  };

  // 追加一条已定稿的 tool/result, 连同工具的附加上下文, 全部在锁内成对落地。
  auto appendResultFor = [&](const ToolCallBlock& call, ToolResult& result,
                             size_t callSeq) {
    std::lock_guard<std::mutex> lock(mtx);
    ToolResultMessage message;
    message.id =
        MessageId(toolResultMessageId(sessionOwned->id(), turn, step, call.id));
    ToolResultBlock block;
    block.toolCallId = call.id;
    block.content.reserve(result.content.size());
    for (const ContentBlock& item : result.content) {
      block.content.push_back(asToolResultContent(item));
    }
    block.isError = result.isError();
    message.content.push_back(std::move(block));
    message.source = toolSource(call.id);

    ToolResultData data;
    data.turn = turn;
    data.step = step;
    data.message = std::move(message);
    // dsh 形状: error 是 {name, code}, name 是工具名 (人类可读的 outcome 不进日志,
    // 回放从 code 就能路由)。
    if (result.isError()) {
      data.error = ToolResultError{call.name, result.code};
    }
    data.meta = result.meta;
    sessionOwned->append(std::move(data), appendIntent({callSeq}));

    // 工具追加的上下文进 next-step: 循环提醒、文件变更通知都走这条。
    for (UserMessage& context : result.additionalContexts) {
      inboxOwned->append(InboxTarget::NextStep, std::move(context));
    }
  };

  // 一个调用组的并发调度: prepare 串行、dispatch 有界并发池、commit 依模型下标顺序提交。
  //
  // 返回 true 表示这一组以取消收尾 (未启动的组内调用已补合成对, 组内已启动的已提交)。
  auto runGroup = [&](size_t groupStart, size_t groupEnd) -> bool {
    const size_t count = groupEnd - groupStart;
    std::vector<Slot> slots(count);
    std::vector<size_t> callSeqs(count, 0);
    size_t committed = 0;  // 只沿连续下标前进的提交游标。
    size_t started = 0;
    bool aborted = false;
    bool concluded = false;

    // 排空守卫: 无论正常/取消/异常退出, 都必须先等仍在跑的 dispatch 停靠再销毁 slots。
    // 没有它, commit/post-execute 抛异常一路 unwind 时, detached 线程会引用已析构的槽位。
    struct DrainGuard {
      std::mutex& mtx;
      std::condition_variable& cv;
      int& inFlight;
      ~DrainGuard() {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [&] { return inFlight == 0; });
      }
    } drainGuard{schedMtx, schedCv, inFlight};

    // 把第 idx 个已定稿槽位最终化 (post-execute) 并落盘。在锁外跑: post-execute 与
    // session 写入都可能阻塞, 不该挡着 worker 停靠。
    auto commitOne = [&](size_t idx) {
      Slot& slot = slots[idx];
      ToolResult finalResult = slot.needsPost
                                   ? deps.tools->commit(slot.exec,
                                                        std::move(slot.result))
                                   : std::move(slot.result);
      appendResultFor(calls[groupStart + idx], finalResult, callSeqs[idx]);
      concluded = concluded || finalResult.concludesTurn;
    };

    // 反复向前清扫: 只要当前下标已定稿就提交它并前进, 直至遇到缺口停住。
    auto commitReady = [&]() {
      for (;;) {
        bool ready = false;
        {
          std::lock_guard<std::mutex> lock(schedMtx);
          if (committed < count && slots[committed].settled) ready = true;
        }
        if (!ready) break;
        commitOne(committed);
        {
          std::lock_guard<std::mutex> lock(schedMtx);
          ++committed;
          schedCv.notify_all();
        }
      }
    };

    for (size_t i = 0; i < count; ++i) {
      if (aborted) break;
      const ToolCallBlock& call = calls[groupStart + i];

      // tool/call 先落盘并计入 started: 之后该调用必然有一条 result (真值或合成值)。
      callSeqs[i] = appendCall(call);
      ++started;
      slots[i].exec = makeExec(call);

      // 阶段一: prepare 串行、可阻塞 (审批)。锁外跑, 与 worker 的 dispatch 只共享
      // 只读的注册表, Chain.run 的本地快照保证并发安全。
      Prepared prepared = deps.tools->prepare(slots[i].exec);
      if (signal->aborted()) {
        // prepare 期间取消到来且未派发: 就地合成一条 aborted-before-dispatch, 收尾。
        slots[i].result = toolError(
            ToolOutcome::AbortedBeforeDispatch,
            "tool call aborted before dispatch", TOOL_CODE_ABORTED_BEFORE_DISPATCH);
        slots[i].settled = true;
        aborted = true;
        break;
      }

      if (std::holds_alternative<PreparedDispatch>(prepared)) {
        // 阶段二: dispatch 交给 worker 线程, 与兄弟调用重叠。最大并发由调度锁守住,
        // 槽位与 inFlight 都在锁内记账, 锁外只跑工具体。
        {
          std::lock_guard<std::mutex> lock(schedMtx);
          ++inFlight;
        }
        std::thread([&, i]() {
          ToolResult result;
          try {
            result = deps.tools->dispatch(slots[i].exec);
          } catch (const std::exception& e) {
            result = toolError(
                ToolOutcome::Crashed,
                std::string("tool crashed: ") + e.what(), TOOL_CODE_CRASHED);
          } catch (...) {
            result = toolError(ToolOutcome::Crashed, "tool crashed",
                               TOOL_CODE_CRASHED);
          }
          {
            std::lock_guard<std::mutex> lock(schedMtx);
            slots[i].needsPost = true;
            slots[i].result = std::move(result);
            slots[i].settled = true;
            --inFlight;
          }
          schedCv.notify_all();
        }).detach();
      } else if (auto* post = std::get_if<PreparedPostResult>(&prepared)) {
        {
          std::lock_guard<std::mutex> lock(schedMtx);
          slots[i].needsPost = true;
          slots[i].result = std::move(post->result);
          slots[i].settled = true;
        }
      } else {
        auto* fin = std::get_if<PreparedFinalResult>(&prepared);
        {
          std::lock_guard<std::mutex> lock(schedMtx);
          slots[i].result = std::move(fin->result);
          slots[i].settled = true;
        }
      }

      commitReady();

      // 有界滚动池: 在启动下一个调用前, 等池内并发降到上限以内 (取消则提前让出)。
      {
        std::unique_lock<std::mutex> lock(schedMtx);
        schedCv.wait(lock,
                     [&] { return inFlight < maxParallel || signal->aborted(); });
        if (signal->aborted()) aborted = true;
      }
    }

    // 排空仍在跑的 dispatch, 随后把已启动的连续调用全部提交。
    {
      std::unique_lock<std::mutex> lock(schedMtx);
      schedCv.wait(lock, [&] { return inFlight == 0; });
    }
    commitReady();

    outcome.concluded = outcome.concluded || concluded;
    if (aborted) {
      // 组内 [0, started) 均已落真值结果 (排空后 commitReady 保证连续提交到 started);
      // 组内其余未启动的调用补写合成的 call/result 对。
      for (size_t i = started; i < count; ++i) {
        appendSkippedToolCall(calls[groupStart + i], turn, step);
      }
      outcome.aborted = true;
      return true;
    }
    if (committed != started) {
      throw std::runtime_error("工具调度器: 有已启动的调用未提交");
    }
    return false;
  };

  size_t next = 0;
  while (next < calls.size()) {
    // 首调用的并发模式定组形: Parallel 组 = 其后最大一段连续 Parallel; Exclusive 自成屏障。
    const bool parallel =
        deps.tools->executionMode(makeExec(calls[next])) == ExecutionMode::Parallel;
    const size_t groupStart = next;
    if (parallel) {
      while (next < calls.size()) {
        if (deps.tools->executionMode(makeExec(calls[next])) !=
            ExecutionMode::Parallel) {
          break;
        }
        ++next;
      }
    } else {
      ++next;
    }

    // 独占屏障或并行组都经由同一个调度器; 组形只是决定它的跨度。
    if (runGroup(groupStart, next)) {
      // 取消级联: 被跳过组的后续全部模型调用补写合成对, 返回 (信号仍保持已取消)。
      for (size_t i = next; i < calls.size(); ++i) {
        appendSkippedToolCall(calls[i], turn, step);
      }
      return outcome;
    }
  }
  return outcome;
}

void ReactLoopAgent::reportError(int turn, int step, const std::string& message) {
  AgentErrorPayload payload;
  payload.agent = this;
  payload.turn = turn;
  payload.step = step;
  payload.message = message;
  deps.points->error.emit(payload, scope());
}

}
