#pragma once

// ============================================================================
// 待处理输入队列。
//
// 对齐 dsh 的 packages/core/agent/src/inbox.ts。
//
// 两条队列就是全部输入语义:
//   nextTurn  排队的后续轮次。FIFO, 每条各占一个 turn。
//   nextStep  当前 turn 的插队/注入池。turn 开头与每个 step 边界全量取走。
//
// claim 的规则: 全部 next-step + (turn 开头时) 恰好一条 next-turn。
//
// 它是**日志的投影**: 每次变更先落一条 agent/inbox/spliced 事件, 再改活队列。于是
// 「有什么没做完」在崩溃重启后仍然知道, 而同步观察者看到的是变更前的队列 —— 它们能从
// 事件里的规范化坐标重建出被移除的消息。
//
// 线程约定: 本类**不自己加锁**。它与相位机同属一个临界区, 由 ReactLoopAgent 统一串行化
// (两处各自加锁会引入锁嵌套, 而 Session::append 的观察者回调就在这个临界区里)。
// ============================================================================

#include <cstddef>
#include <functional>
#include <vector>

#include "Session.hpp"
#include "SessionTypes.hpp"

namespace avox {

// 队列变更的通知。
//
// claim 只发 claimed: 被取走的消息是**被消费**了, 不是被取消, 所以既不发 discarded 也
// 不在事件里记 canceled。这个区分让「用户的话被丢了」与「用户的话被处理了」在日志与 UI
// 上不会混淆。
struct InboxNotifications {
  std::function<void(const UserMessage&)> inserted;
  std::function<void(const UserMessage&)> discarded;
  std::function<void(const UserMessage&, int turn)> claimed;
};

class Inbox {
 public:
  // session: 借用引用, 必须比本对象活得久。
  //
  // 构造时从 session.events() 里的 agent/inbox/spliced 事件折叠出当前队列 —— resume
  // 出来的会话因此恢复它未做完的工作, 且折叠期间不发任何通知 (那些消息不是刚到的)。
  Inbox(Session& session, InboxNotifications notifications);

  Inbox(const Inbox&) = delete;
  Inbox& operator=(const Inbox&) = delete;

  const std::vector<UserMessage>& nextTurn() const { return nextTurnQueue; }
  const std::vector<UserMessage>& nextStep() const { return nextStepQueue; }

  bool hasPending() const {
    return !nextTurnQueue.empty() || !nextStepQueue.empty();
  }

  // 取走本 step 要处理的消息。
  //
  // 永远先全量取 next-step; target 为 NextTurn 时再取**一条** next-turn。
  // 纯删除语义: 不发 discarded, 不在事件里标 canceled。
  std::vector<UserMessage> claim(InboxTarget target, int turn);

  // 标准 splice 语义 + 持久记录。
  //
  // start 超出队列长度时截断为「追加到尾部」; deleteCount 截断到可删除的上限。
  // deleteCount 为 0 且 inserted 为空时是空操作 (不落事件)。
  //
  // 返回被移除的消息。
  std::vector<UserMessage> splice(InboxTarget target, size_t start,
                                  size_t deleteCount,
                                  std::vector<UserMessage> inserted);

  // 追加到队列尾部。
  void append(InboxTarget target, UserMessage message);

  // 插入到队列头部。
  void prepend(InboxTarget target, UserMessage message);

  // 移除一条待处理消息并记为取消; 返回它是否还在队列里。
  bool remove(const MessageId& id);

  // 清空两条队列并记为取消 (cancel 的默认行为)。
  void clear();

 private:
  struct Location {
    InboxTarget target;
    size_t index;
  };

  std::vector<UserMessage>& queueOf(InboxTarget target);
  const std::vector<UserMessage>& queueOf(InboxTarget target) const;

  // 定位一条待处理消息; 不在队列里返回 false。
  bool locate(const MessageId& id, Location& out) const;

  // 提交一次规范化变更并发布通知。
  std::vector<UserMessage> mutate(InboxTarget target, size_t start,
                                  size_t deleteCount,
                                  std::vector<UserMessage> inserted,
                                  bool discardRemoved);

  // 把一条持久 splice 应用到投影上 (折叠用, 不落事件不通知)。
  void apply(const InboxSplicedData& splice);

  // 校验一次候选变更: 同一消息 id 不得在两条队列里同时待处理。
  void validate(const InboxSplicedData& splice) const;

  Session& session;
  InboxNotifications notifications;
  std::vector<UserMessage> nextTurnQueue;
  std::vector<UserMessage> nextStepQueue;
};

}
