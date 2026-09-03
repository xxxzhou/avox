#pragma once

// ============================================================================
// Team 门面: 工具层看到的全部队伍操作 (dsh packages/experimental/agent-team
// src/index.ts 的 TeamService 组合)。
//
// 组合关系 (声明序即构造序):
//   TeamActivity  变更等待原语
//   TeamJournal   Lead 日志上的事务与折叠状态
//   TeamRoster    continuable 队友的存活注册表
//   TeamMailbox   持久邮箱 (排队/投递/补投)
//   TeamTaskBoard 共享任务板 (CAS)
//
// 生命周期: AgentHost 在 openAgent (Lead 会话就绪) 后 createTeamService 创建;
// closeAgent/shutdown 前调 dispose —— 先 close activity (放掉 waiter), 再按预算
// 停全部队友。恢复语义: openAgent 后调 recover() (重折叠 + provisioning 冷裁定
// + 持久邮箱补投)。
//
// 调用方契约: 每个操作先 membershipOf(caller) 解析归属 (非成员一律
// TEAM_MEMBER_NOT_FOUND —— 含队友派生的 one-shot 子代理)。
// ============================================================================

#include <memory>
#include <string>
#include <vector>

#include "TeamActivity.hpp"
#include "TeamJournal.hpp"
#include "TeamMailbox.hpp"
#include "TeamRoster.hpp"
#include "TeamTaskBoard.hpp"
#include "TeamTypes.hpp"

namespace avox {

class TeamService {
 public:
  // leadWriter: Lead 会话的落盘观察者 (AgentHost 借出; 生命周期覆盖本对象)。
  TeamService(TeamConfig config, TeamRosterDeps deps,
              SessionWriter& leadWriter);

  TeamService(const TeamService&) = delete;
  TeamService& operator=(const TeamService&) = delete;

  // ---- 归属 ----

  // 调用 agent 的队伍归属。Lead -> {lead, "lead"}; active 队友 -> {root, 名字};
  // 其余 (provisioning/failed 成员、one-shot 子代理) TEAM_MEMBER_NOT_FOUND。
  TeamMembership membershipOf(const Agent* caller) const;

  // ---- 队友 ----

  TeamMemberSnapshot spawnTeammate(const SpawnTeammateRequest& request);
  std::vector<TeamMemberView> listMembers() const;
  InterruptResult interruptAgent(const TeamMembership& membership,
                                 const std::string& target);

  // ---- 消息 ----

  SendTeamMessageResult sendMessage(const Agent& sender,
                                    const SendTeamMessageRequest& request);

  // ---- 任务 ----

  TeamTaskSnapshot createTask(const CreateTeamTaskRequest& request);
  TeamTaskSnapshot getTask(const std::string& taskId) const;
  std::vector<TeamTaskView> listTasks() const;
  TeamTaskSnapshot updateTask(const TeamMembership& membership,
                              const UpdateTeamTaskRequest& request);

  // ---- 等待与生命周期 ----

  // 等下一次队伍变更 (list_agents / team_task_list 前的长轮询)。
  TeamWaitResult waitForChange(int64_t timeoutMs);

  // 免区间校验的单片等待: 工具层把长等待切片以响应取消信号 (整体预算的
  // 10s..1h 契约由工具入口自查)。
  TeamWaitResult pollForChange(int64_t maxMs);

  // 恢复: 重折叠 Lead 日志 -> provisioning 冷裁定 -> 持久邮箱补投。
  // openAgent 后调用一次。
  void recover();

  // 销毁: 放掉 waiter, 按配置预算停全部队友。幂等。
  void dispose();

  TeamConfig config() const { return teamConfig; }
  TeamJournal& journal() { return teamJournal; }
  TeamRoster& roster() { return teamRoster; }

 private:
  // 目标名 -> 成员快照 (按名或 id; 未知 TEAM_INVALID_TARGET)。
  const TeamMemberSnapshot* memberForTarget(const std::string& target) const;

  TeamConfig teamConfig;
  Agent& lead;
  TeamActivity activity;
  TeamJournal teamJournal;
  TeamRoster teamRoster;
  TeamMailbox mailbox;
  TeamTaskBoard taskBoard;
};

}
