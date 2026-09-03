#pragma once

// ============================================================================
// 共享任务板: team/task 事件上的 CAS (compare-and-swap) 变更。
//
// 对齐 dsh packages/experimental/agent-team/src/task-board.ts。
//
// 每次变更以 expectedRevision 做乐观并发控制: 折叠状态里的 revision 不等即
// TEAM_TASK_STALE_REVISION —— 两个成员同时 claim/complete 同一任务时, 后提交者
// 响亮失败, 模型重读后重试。动作合法性 (状态机 + 依赖 + 归属) 全在事务内校验,
// 通过即 append revision+1 的新快照 (折叠保证连续性)。
//
// writeScopes: 非空时限制哪些队友名可变更/认领 (Lead 豁免); 名单外的变更
// TEAM_TASK_UNAUTHORIZED。名单里出现不存在的成员名 -> 视图层的 writeScopeWarnings。
// ============================================================================

#include <map>
#include <string>
#include <vector>

#include "TeamError.hpp"
#include "TeamJournal.hpp"
#include "TeamTaskGraph.hpp"
#include "TeamTypes.hpp"

namespace avox {

class TeamTaskBoard {
 public:
  TeamTaskBoard(Agent& lead, TeamConfig config);

  // 创建任务 (revision 1)。subject 必填; blockedBy 引用不存在的任务抛
  // TEAM_TASK_NOT_FOUND, 引用已删除任务抛 TEAM_TASK_DELETED, 超上限抛
  // TEAM_TASK_LIMIT。
  TeamTaskSnapshot create(TeamJournal& journal, const CreateTeamTaskRequest& request);

  // CAS 变更。期望 revision 不符抛 TEAM_TASK_STALE_REVISION; 状态机/依赖/归属
  // 违规按 dsh 映射错误码; 成功返回新快照。
  TeamTaskSnapshot update(TeamJournal& journal, const TeamMembership& membership,
                          const UpdateTeamTaskRequest& request);

  // 单条快照 (只读, 事务外安全); 不存在抛 TEAM_TASK_NOT_FOUND, 已删抛
  // TEAM_TASK_DELETED。
  TeamTaskSnapshot get(const TeamJournal& journal, const std::string& taskId) const;

  // 视图列表 (只读): 含 owner 名、就绪度与 write-scope 警告。
  std::vector<TeamTaskView> list(const TeamJournal& journal) const;

 private:
  // 折叠状态 -> 任务 id 索引表 (图校验入参)。
  static std::map<std::string, TeamTaskSnapshot> taskMapOf(
      const TeamFoldState& state);

  // 图校验异常 -> TeamError 映射。
  static TeamError mapGraphError(const TeamTaskGraphError& e);

  Agent& lead;
  TeamConfig config;
};

}
