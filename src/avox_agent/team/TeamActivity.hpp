#pragma once

// ============================================================================
// 队伍变更的等待原语。
//
// 对齐 dsh packages/experimental/agent-team/src/activity.ts。
//
// 每次持久变更提交 (journal appendAndFlush 成功) 都唤醒全部 waiter ——
// wait_agent 的语义是「从本调用开始观察之后的变更」, 不区分变更类别 (状态/邮箱/
// 任务板), 重读即见。销毁 (close) 释放所有 waiter (TEAM_WAIT_ABORTED)。
//
// 锁序: journal 锁 -> 本锁 (notify 在事务提交点调用); waiter 只持本锁 ——
// 本锁是叶子锁, 持它不得再取任何 agent/journal 锁。
// ============================================================================

#include <condition_variable>
#include <mutex>

#include "TeamError.hpp"
#include "TeamTypes.hpp"

namespace avox {

class TeamActivity {
 public:
  // 等待下一次队伍变更; 超时返回 {timedOut=true}, 队伍销毁抛 TEAM_WAIT_ABORTED,
  // 入参超时区间非法抛 TEAM_INVALID_TIMEOUT (dsh: 10s..1h)。
  TeamWaitResult wait(int64_t timeoutMs);

  // 免区间校验的单片等待: 供工具层把长等待切片以保持对取消信号的响应
  // (整体预算的 10s..1h 契约由调用方在入口校验)。
  TeamWaitResult poll(int64_t maxMs);

  // 一次变更已提交: 唤醒全部 waiter (journal 事务提交点调用, 持 journal 锁)。
  void notifyChanged();

  // 队伍销毁: 释放全部 waiter (抛 TEAM_WAIT_ABORTED)。
  void close();

 private:
  std::mutex mtx;
  std::condition_variable cv;
  // 变更代数: waiter 记住进入时的值, 醒来发现它变了即「有变更」。
  uint64_t changeEpoch = 0;
  bool closed = false;
};

}
