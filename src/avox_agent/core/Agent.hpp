#pragma once

// ============================================================================
// Agent 句柄与扩展点词汇表。
//
// 对齐 dsh 的 packages/core/agent/src/runtime-types.ts。
//
// Agent 是一个**句柄**, 不是驱动器: 它暴露最小驱动原语 (投递输入、取消、等静止), 而
// turn/step 的推进完全由内部驱动 (ReactLoopAgent) 拥有。刻意没有公开的 step() ——
// 驱动是唯一 claim 输入、唯一写 turn/step 事件的地方。
//
// 输入语义只有两个自由维度: 进哪条队列 × 是否唤醒驱动。followup / steer / inject 是
// 这两个维度的三个固定预设, 所以做成非虚的便利方法。
// ============================================================================

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "Abort.hpp"
#include "Dispatch.hpp"
#include "Inbox.hpp"
#include "Scope.hpp"
#include "Session.hpp"
#include "SessionTypes.hpp"

namespace avox {

// Agent 的路由与容量选项。
struct AgentOptions {
  std::string provider;
  std::string model;
  std::optional<int> maxTokens;
  // 委派深度的单调地板 (dsh subagentDepth): 子代理创建子 ReactLoopAgent 时取
  // max(父会话头 delegationDepth, 本值) + 1 作为子深度。描述符不持久化它 ——
  // 冷恢复信任头行的 delegationDepth (见 SubagentDescriptorData)。
  int subagentDepth = 0;
};

// 生命周期状态。
//
// 只有两种: 销毁不是第三种可观察状态 (它把 agent 从注册表里移除)。维护相位对外也报 Idle
// —— 压缩 / clear 这类任务独占 agent 时不该让 UI 闪一下 running。
enum class AgentStatus { Idle, Running };

// ---------------------------------------------------------------------------
// Agent 句柄
// ---------------------------------------------------------------------------

class Agent {
 public:
  virtual ~Agent() = default;

  // 与 session 共享的同一个身份。
  virtual const SessionId& id() const = 0;
  virtual const AgentOptions& options() const = 0;

  // 本 agent 驱动的会话; 它的日志是持久真相源。
  //
  // 直接访问只在**确知没有驱动在并发写**时安全 (装配期、shutdown 之后)。运行期要写日志
  // 请用 withSession。
  virtual Session& session() = 0;

  // 在 agent 的会话临界区内执行一段代码。
  //
  // Session 不是线程安全的, 而多个组件要写它: 驱动写边界与消息事件, 审批服务写审批对,
  // 压缩写替换节点, 宿主 send 写 inbox splice。它们都必须经由这里串行化。
  //
  // 契约: fn 内不得再调用本 agent 的其它方法 (会死锁), 也不该做阻塞的长任务 —— 它会挡住
  // 驱动。审批那种「要等人」的等待必须放在调用 withSession **之前或之后**, 而不是里面。
  virtual void withSession(const std::function<void(Session&)>& fn) = 0;

  // 持久待处理工作的投影。
  virtual Inbox& inbox() = 0;

  virtual AgentStatus status() const = 0;

  // 本 agent 的注册作用域: 挂在这里的贡献是 agent 局部的, 随 agent 销毁而撤销。
  virtual ScopeKey scope() const = 0;

  // 清空排队与插队的工作 (除非 keepInbox), 并取消当前活动。
  //
  // 第一个原因胜出。**无活动时是空操作**, 不会「预约」取消后续工作 —— 否则一次早到的
  // 取消会莫名杀掉用户随后发起的下一轮。
  virtual void cancel(AgentCancelCause cause, bool keepInbox = false) = 0;

  // 阻塞到当前整体活动到达静止。
  //
  // 会跟随「在被观察的驱动退休前又起来的替换工作」, 但不标识任何一条特定消息的结束。
  // timeoutMs < 0 表示无限等; 返回是否已静止。
  //
  // 宿主语言 (Python) 调用时务必给一个有限超时并在等待期间释放 GIL, 否则解释器连
  // Ctrl+C 都收不到。
  virtual bool whenIdle(int timeoutMs = -1) = 0;

  // 从真正的 idle 相位跑一个非 turn 的维护任务 (压缩、clear)。
  //
  // 任务同步抢占 idle 相位; 期间到达的唤醒输入留在 inbox 里, 任务结束后按需补放。
  // 公开状态在此期间仍报 Idle。
  //
  // 已有驱动或另一个维护任务占用时返回 false 而不执行。
  virtual bool runMaintenance(
      const std::function<void(const std::shared_ptr<AbortSignal>&)>& task) = 0;

  // 把输入投递到某条 inbox 边界, 并决定是否唤醒驱动。
  //
  // 在活动已取消之后投递的唤醒输入会排到下一个 turn, 等那个正在死掉的活动收敛到 idle
  // 后再跑; disposed 原因下则原地停放。
  virtual void send(UserMessage message, InboxTarget target, bool wakeup) = 0;

  // 排一个普通的后续轮次并唤醒驱动。该条成为它自己那个 turn 的唯一普通消息。
  void followup(UserMessage message) {
    send(std::move(message), InboxTarget::NextTurn, true);
  }

  // 为最近的一个 step 投递插话。idle 驱动会因此起一个 turn; 运行中的驱动在下一个 step
  // 边界消费它。
  //
  // 这是「工具跑到一半时用户改主意」的通道 —— 不必打断整轮。
  void steer(UserMessage message) {
    send(std::move(message), InboxTarget::NextStep, true);
  }

  // 为下一次 pre-step 排入模型可见上下文, **不唤醒**驱动。
  //
  // 运行中的驱动会在最近的后续 step 边界取走它; idle 时它一直待着, 直到别的输入唤醒
  // 驱动。可能错过一个 pre-step 已经取完批次的请求。
  void inject(UserMessage message) {
    send(std::move(message), InboxTarget::NextStep, false);
  }
};

// ---------------------------------------------------------------------------
// 扩展点载荷与决策
// ---------------------------------------------------------------------------

// agent/pre-step: 是否进入这一步, 以及带哪些消息进。
struct PreStepReject {};
struct PreStepEnter {
  std::vector<UserMessage> messages;
};
using PreStepDecision = std::variant<PreStepReject, PreStepEnter>;

struct PreStepPayload {
  Agent* agent = nullptr;
  // 已从 inbox 取出的批次。监听器可以替换整份列表 (返回 PreStepEnter 时)。
  std::vector<UserMessage> messages;
  int turn = 0;
  int step = 0;
  std::shared_ptr<AbortSignal> signal;
};

// agent/request: 替换本次请求的模型配置。
//
// **不能改 messages** —— 模型可见内容必须走已记录的通道 (约束: 模型可见 ⟺ 已记录)。
struct RequestPayload {
  Agent* agent = nullptr;
  int turn = 0;
  int step = 0;
  std::shared_ptr<AbortSignal> signal;
};

// agent/request-error: 一次失败的模型请求由谁负责恢复。
//
// 返回 Retry 表示本监听器接管恢复 (通常已经做过退避或换路由); 返回 nullopt 让失败终结。
// 重试发生在**同一个 step 内**的循环里, turn/step 编号不变 —— 于是日志里能看出
// 「同一步重试了三次」。
struct RequestRetry {};
using RequestErrorAction = std::optional<RequestRetry>;

struct RequestErrorPayload {
  Agent* agent = nullptr;
  int turn = 0;
  int step = 0;
  std::string provider;
  LlmFailure failure;
  std::shared_ptr<AbortSignal> signal;
};

// agent/turn-stopping: turn 即将关闭。
//
// 没有返回值。反对的唯一方式是 agent->steer(...), 驱动随后**重读 inbox** 决定是否再跑
// 一步。于是数据决定, 监听器顺序不影响结果。
//
// turn 预算与循环卫生挂在这里 —— 而不是在驱动里硬编码一个步数上限。
struct TurnStoppingPayload {
  Agent* agent = nullptr;
  int turn = 0;
  std::shared_ptr<AbortSignal> signal;
};

// agent/error: 一个 step 或 turn 出错了 (只上报, 不能否决)。
//
// 即使失败没有 turn 内位置也要有个上报口, 所以它是通知而不是日志事件。
struct AgentErrorPayload {
  Agent* agent = nullptr;
  int turn = 0;
  int step = 0;
  std::string message;
};

// agent/status: 状态翻转 (idle <-> running)。
struct AgentStatusPayload {
  Agent* agent = nullptr;
  AgentStatus status = AgentStatus::Idle;
};

// agent/created、agent/disposed。
struct AgentLifecyclePayload {
  Agent* agent = nullptr;
};

// 全部 agent 级扩展点的集合。
//
// 由宿主 (compose/) 持有一份, 交给驱动使用; 策略插件往这里注册。
struct AgentExtensionPoints {
  Chain<PreStepPayload, PreStepDecision> preStep{"agent/pre-step"};
  Chain<RequestPayload, LlmCallConfig> request{"agent/request"};
  Chain<RequestErrorPayload, RequestErrorAction> requestError{
      "agent/request-error"};
  Serial<TurnStoppingPayload> turnStopping{"agent/turn-stopping"};
  Notify<AgentErrorPayload> error{"agent/error"};
  Notify<AgentStatusPayload> status{"agent/status"};
  Notify<AgentLifecyclePayload> created{"agent/created"};
  Notify<AgentLifecyclePayload> disposed{"agent/disposed"};
};

}
