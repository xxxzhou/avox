#pragma once

// ============================================================================
// Lead 日志 -> 队伍状态的折叠 (fold)。
//
// 对齐 dsh packages/experimental/agent-team/src/fold.ts。
//
// 重放即状态: 名册 / 任务板 / 邮箱全部从 team/* 事件序列重建。折叠不变式被
// 违反时抛异常 —— 一份 Lead 日志要么折叠出一个合法队伍, 要么让 resume 响亮失败,
// 不存在「静默恢复出一个被掏空的队伍」。
//
// 与 dsh 的对应:
//   TeamFoldState { members, tasks, queued, delivered, nextTaskNumber,
//                   nextMessageNumber }
//   成员名永不复用; name/provider/context 不可变; phase 必须从 provisioning 开始,
//   且只有 provisioning -> active | failed 一次收敛; 任务 revision 从 1 起 +1 连续;
//   数字 id task-<n> 推进 nextTaskNumber, msg-<n> 推进 nextMessageNumber;
//   消息只入队一次; delivered 要求先入队、目标匹配、不重复; teamId != 根 id 的事件
//   整条忽略 (fork 前缀里继承的别队状态)。
// ============================================================================

#include <map>
#include <set>
#include <string>
#include <vector>

#include "avox_agent/core/SessionTypes.hpp"

namespace avox {

// 折叠出的队伍状态 (事务内可变; 提交即已 append 完毕)。
struct TeamFoldState {
  // 本队伍 (= Lead 会话) 的 id: 增量折叠时用它判 fork 前缀 (teamId 不符整条忽略)。
  SessionId rootId;
  // 按 id 索引的成员快照 (插入序 = spawn 序)。
  std::vector<TeamMemberSnapshot> members;
  // 按 id 索引的任务快照 (插入序 = 创建序; task-<n> 的 n 单调)。
  std::vector<TeamTaskSnapshot> tasks;
  // 待投递 (或投递未确认) 的消息, 按入队序。
  std::vector<TeamMessageSnapshot> queued;
  // 已确认投递的 messageId 集。
  std::set<std::string> delivered;
  // 下一个任务的数字后缀 (从既有 id 推进, 恒 > 已用过的最大值)。
  int64_t nextTaskNumber = 1;
  // 下一个消息的数字后缀 (同上, msg-<n>)。
  int64_t nextMessageNumber = 1;

  // ---- 查询便利 (事务与命令层共用) ----

  const TeamMemberSnapshot* memberById(const SessionId& id) const;
  TeamMemberSnapshot* memberById(const SessionId& id);
  // 名字查成员 (名字不可复用, 命中至多一个 —— 含 failed 成员)。
  const TeamMemberSnapshot* memberByName(const std::string& name) const;
  const TeamTaskSnapshot* taskById(const std::string& id) const;
  TeamTaskSnapshot* taskById(const std::string& id);
};

// 把 Lead 会话日志折叠成状态。rootId: 本队伍 (Lead 会话) 的 id —— teamId 不符的
// team/* 事件整条忽略。违反不变式抛 std::runtime_error。
TeamFoldState foldTeam(const SessionId& rootId,
                       const std::vector<SessionEvent>& events);

// 把一条已 append 的 team/* 事件折进状态 (journal 在事务内对增量事件调用;
// teamId 不符时静默忽略 —— 与 foldTeam 同规则)。
void foldTeamEvent(TeamFoldState& state, const SessionEvent& event);

}
