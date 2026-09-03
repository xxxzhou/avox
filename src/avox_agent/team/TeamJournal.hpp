#pragma once

// ============================================================================
// Lead 会话日志 = 队伍状态的唯一真相 (team/* 事件的落点)。
//
// 对齐 dsh packages/experimental/agent-team/src/journal.ts。
//
// dsh 用 per-Lead 的 promise 链串行化事务; C++ 用一把 journalMtx —— 语义相同:
//   1. 全部变更 (member/task/queued/delivered) 在事务内读状态、校验、append;
//   2. appendAndFlush 把事件写进 **Lead 会话日志** (经 withSession 串行进 Lead 的
//      会话临界区), 折叠增量, 刷盘, 唤醒 waiter —— 返回即已持久;
//   3. 状态是日志的投影 (fold), 不是独立存储。
//
// 锁序 (全系统唯一合法顺序, 反向即死锁):
//   journalMtx -> Lead mtx (withSession) -> SessionWriter mtx (观察者)
//   journalMtx -> 队友 mtx (投递 send)
//   journalMtx -> activity mtx (notify)
//   持以上任何锁不得反向取 journalMtx —— 会话观察者在 Lead 锁内运行, 因此观察者
//   **永不**触碰 journal。
// ============================================================================

#include <functional>
#include <mutex>
#include <string>

#include "avox_agent/core/Agent.hpp"
#include "avox_agent/core/SessionPersistence.hpp"
#include "TeamActivity.hpp"
#include "TeamFold.hpp"

namespace avox {

class TeamJournal {
 public:
  // lead: Lead agent (借用, 生命周期由 AgentHost 覆盖本对象)。
  // writer: Lead 会话的落盘观察者 (借用, 同上)。
  // activity: 变更等待原语 (借用, 由 TeamService 持有)。
  TeamJournal(Agent& lead, SessionWriter& writer, TeamActivity& activity);

  // 串行执行一段队伍变更。fn 内可用 state() / appendAndFlush()。
  // dsh transact 的对应物; 异常原样传播 (状态与日志不受影响 —— append 是原子提交)。
  void transact(const std::function<void()>& fn);

  // 当前折叠状态。**仅事务内有效/可变** —— 事务外的读取也必须经 transact 包裹。
  TeamFoldState& state();

  // 只读快照访问 (锁 journalMtx, 与 transact 互斥)。工具处理线程与提示词渲染线程
  // 在事务外读队伍状态用这个; fn 内不得调用任何写方法。
  void readState(const std::function<void(const TeamFoldState&)>& fn) const;

  // 在 Lead 日志追加一条 team/* 事件并持久化: append (Lead 会话临界区) ->
  // 增量折叠 -> writer flush -> 唤醒 waiter。**仅事务内调用**。
  // 折叠抛异常即程序错误 (本层只产出满足不变式的事件), 原样传播。
  void appendAndFlush(const EventData& data);

  // 从 Lead 日志整份重折叠 (resume / fork 前缀消化后调用)。事务内执行。
  void reloadFromLead();

 private:
  Agent& lead;
  SessionWriter& writer;
  TeamActivity& activity;
  // readState 是 const 成员, 锁须 mutable。
  mutable std::mutex journalMtx;
  TeamFoldState foldState;
};

}
