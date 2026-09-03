#pragma once

// ============================================================================
// 持久邮箱: 排队 (team/message/queued) -> 投递确认 (team/message/delivered)。
//
// 对齐 dsh packages/experimental/agent-team/src/mailbox.ts。
//
// 排队即持久: 消息先进 Lead 日志, 再尽力投递; 投递的确认标记也进 Lead 日志。
// queued 减 delivered = 恢复邮箱 —— 进程重启后由恢复流程补投 (wakeup 唤醒冷恢复,
// quiet 只投活成员)。
//
// 投递语义 (avox 输入 API 的两个维度, 见 Agent.hpp):
//   quiet   -> send(NextStep, wakeup=false)  = inject: 不唤醒空闲目标
//   wakeup  -> send(NextTurn, wakeup=true)   = followup: 起一轮
//
// 投递确认是**同步校验**: send() 在目标会话临界区内落 inbox splice 事件, 返回后
// 扫描目标日志找到该消息即确认, 再落 delivered —— 与 dsh 的 session 事件观察者
// 等价, 但无异步窗口 (锁序: journalMtx -> 目标 agent mtx, 见 TeamJournal.hpp)。
//
// deliveryContent 框头 (dsh 原文): "Team message <id> from <senderName>:\n" + 正文;
// maxMessageBytes 约束的是**完整帧**的 UTF-8 字节数。
// ============================================================================

#include <string>
#include <vector>

#include "avox_agent/core/Agent.hpp"
#include "TeamError.hpp"
#include "TeamJournal.hpp"
#include "TeamRoster.hpp"
#include "TeamTypes.hpp"

namespace avox {

class TeamMailbox {
 public:
  // lead: Lead agent (投递目标之一, 恒活)。
  TeamMailbox(Agent& lead, TeamConfig config);

  // 发送一条消息: 解析目标 -> 校验 (self/大小/邮箱上限) -> 入队 -> 尽力投递。
  // 校验失败抛 TeamError; 结果 accepted=false 表示已持久排队但尚未投递
  // (目标不在/冷恢复失败/确认未见 —— 恢复流程或下次交互补投)。
  //
  // senderName: 发送方队伍内名字 (lead 或队友名), 进消息快照与投递帧头。
  SendTeamMessageResult send(TeamJournal& journal, TeamRoster& roster,
                             const Agent& sender, const std::string& senderName,
                             const SendTeamMessageRequest& request);

  // 恢复补投: 把 queued 减 delivered 里目标仍活 (或 wakeup 可冷恢复) 的消息投出去。
  // 在 journal.transact 内由恢复流程调用; 单条失败记 warn 继续 (下次恢复再试)。
  void dispatchPending(TeamJournal& journal, TeamRoster& roster);

  // 一个成员的 pending 数 (queued 减 delivered) —— send 的上限检查用。
  static size_t pendingCountFor(const TeamFoldState& state,
                                const SessionId& targetId);

 private:
  // 投递一条已入队消息到活目标; 确认后落 delivered。
  // 返回是否确认投递 (未确认 = 仍排队)。journalMtx 已由调用方持有。
  bool deliverOnce(TeamJournal& journal, Agent& target,
                   const TeamMessageSnapshot& message);

  Agent& lead;
  TeamConfig config;
};

}
