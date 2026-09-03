#pragma once

// ============================================================================
// turn/step 驱动。
//
// 对齐 dsh 的 packages/core/agent-loop/src/agent.ts (ReactLoopAgent)。
//
// 相位机是骨架, 不是细节:
//   Idle         无驱动在跑。
//   Maintenance  非 turn 的维护任务独占 agent (压缩、clear)。**对外仍报 Idle** ——
//                否则 UI 会为一次后台压缩闪一下 running。
//   Running      驱动持有 turn/step 与本次活动的取消令牌。
//
// 唤醒的 latch 规则: 驱动活着就自己 claim 队列 (不 latch); 维护中或 abort 之后到达的唤醒
// 才置 wakeRequested, 在驱动自己的收敛边界重放; **disposed 原因永不 latch**, 否则析构会
// 等一个永远不来的新 turn。
//
// 线程模型 (与 dsh 的最大差异 —— dsh 是单线程 async):
//   一个常驻 driver 线程跑 turn/step; 宿主线程通过 send/cancel/whenIdle 与它交互。
//   一把 mtx 同时保护相位、inbox 与 **session** —— Session 本身不是线程安全的, 而两个
//   线程都要 append (驱动写边界事件, 宿主 send 写 inbox splice 事件)。
//   长任务 (模型请求、工具执行) 一律在锁外跑。
//
// 由此产生一条对外契约: **会话观察者的回调运行在 agent 锁内, 不得回调进 agent 的方法**。
// 这沿用旧 AgentClient (已删) 的观察者约定。
// ============================================================================

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include "Abort.hpp"
#include "Agent.hpp"
#include "Inbox.hpp"
#include "Llm.hpp"
#include "Scope.hpp"
#include "Session.hpp"
#include "SystemPrompt.hpp"
#include "ToolRuntime.hpp"

namespace avox {

class ReactLoopAgent : public Agent {
 public:
  // 驱动依赖 (全部借用指针, 生命周期须覆盖本对象)。
  struct Deps {
    SystemPrompt* systemPrompt = nullptr;
    ToolRuntime* tools = nullptr;
    LlmProvider* llm = nullptr;
    AgentExtensionPoints* points = nullptr;
    // 同一批工具调用的最大并发。1 = 串行。
    //
    // 三阶段的价值有一半在「策略串行 / 日志有序」, 即使完全不并发也需要 prepare (审批)
    // 与 commit (后处理) 分离, 所以这里默认 1 是安全的起点。
    int maxParallelToolCalls = 1;
  };

  // session: 转移所有权。agent 与它的会话同生共死 —— 于是不存在「会话先没了而驱动还在
  // 写日志」这个窗口。
  ReactLoopAgent(std::unique_ptr<Session> session, AgentOptions options, Deps deps);
  ~ReactLoopAgent() override;

  ReactLoopAgent(const ReactLoopAgent&) = delete;
  ReactLoopAgent& operator=(const ReactLoopAgent&) = delete;

  // ---- Agent ----
  const SessionId& id() const override;
  const AgentOptions& options() const override { return agentOptions; }
  Session& session() override { return *sessionOwned; }
  void withSession(const std::function<void(Session&)>& fn) override;
  Inbox& inbox() override { return *inboxOwned; }
  AgentStatus status() const override;
  ScopeKey scope() const override { return &agentScope; }

  void cancel(AgentCancelCause cause, bool keepInbox = false) override;
  bool whenIdle(int timeoutMs = -1) override;
  bool runMaintenance(
      const std::function<void(const std::shared_ptr<AbortSignal>&)>& task) override;
  void send(UserMessage message, InboxTarget target, bool wakeup) override;

  // 停止驱动线程并等它退出 (幂等)。析构会调用; 宿主也可显式调以获得确定的收敛点。
  //
  // 返回后保证: 无驱动线程、无在跑的工具、日志不再增长。此后会话观察者可安全释放。
  void shutdown();

 private:
  // ---- 相位 ----

  struct IdlePhase {
    int lastTurn = 0;
  };
  struct MaintenancePhase {
    std::shared_ptr<AbortController> abort;
    int lastTurn = 0;
    bool wakeRequested = false;
  };
  struct RunningPhase {
    std::shared_ptr<AbortController> abort;
    int turn = 0;
    int step = 0;
    bool wakeRequested = false;
  };
  using Phase = std::variant<IdlePhase, MaintenancePhase, RunningPhase>;

  // 一个 step 的准备结果。
  struct PreparedStep {
    bool rejected = false;
    std::vector<UserMessage> messages;
    PromptAssembly assembly;
  };

  // 一次装配好的请求。
  struct BuiltRequest {
    LlmRequest request;
    PreparedLlmCall prepared;
  };

  // 一批工具调用的结局。
  struct ToolCallsOutcome {
    bool concluded = false;
    bool aborted = false;
  };

  // ---- 状态迁移 (均需持 mtx) ----

  AgentStatus statusOfLocked() const;
  void setPhaseLocked(Phase next);
  // 唤醒驱动或把唤醒 latch 起来。
  void wakeDriverLocked(bool wakeAfterAbort);

  // ---- 驱动 ----

  void driverMain();
  void kick(std::shared_ptr<AbortController> controller);
  // 返回是否还要开下一个 turn。
  //
  // controller 按引用传: turn 正常结束而队列仍有工作时, 这里会**换一个新的取消令牌** ——
  // 于是挂在旧令牌上的 latch 自然失效 (活驱动自己会 claim 队列)。
  bool runTurn(std::shared_ptr<AbortController>& controller);
  PreparedStep preStep(InboxTarget target, int turn, int step,
                       const std::shared_ptr<AbortSignal>& signal);
  // nullopt 表示本 step 之后还要继续 (模型请求了工具)。
  std::optional<TurnEndReason> runStep(const PromptAssembly& assembly, int turn,
                                       int step,
                                       const std::shared_ptr<AbortSignal>& signal);
  BuiltRequest buildRequest(int turn, int step, const PromptAssembly& assembly,
                            const std::shared_ptr<AbortSignal>& signal);
  ToolCallsOutcome executeToolCalls(const std::vector<ToolCallBlock>& calls,
                                    int turn, int step,
                                    const std::shared_ptr<AbortSignal>& signal);

  // 为一个未派发的模型调用补写合成的 call/result 对。
  //
  // 目的纯粹是让回放保持合法: 每个 tool_call 必须有对应的 result, 否则下一次请求的消息
  // 序列在后端侧就是非法的 (续发直接 400)。
  void appendSkippedToolCall(const ToolCallBlock& call, int turn, int step);

  void reportError(int turn, int step, const std::string& message);

  AgentOptions agentOptions;
  Deps deps;

  // 声明顺序即构造顺序: inbox 捕获 session 的引用, 所以 session 必须在前。
  std::unique_ptr<Session> sessionOwned;
  std::unique_ptr<Inbox> inboxOwned;
  std::unique_ptr<RuntimeContextProjection> runtimeContext;

  Scope agentScope;

  mutable std::mutex mtx;
  // 驱动等这个 (有唤醒待处理且相位为 Idle)。
  std::condition_variable wakeCv;
  // whenIdle 等这个。
  std::condition_variable idleCv;

  Phase phase{IdlePhase{}};
  bool stopping = false;
  bool pendingWake = false;
  // 驱动正在跑 kick (与相位分开: 相位在 kick 退出前就回到 Idle 也要能等到线程收敛)。
  bool driverActive = false;
  // 本实例是否已写过它的 request/header 锚点。
  bool requestHeaderLogged = false;

  std::thread driverThread;
};

}
