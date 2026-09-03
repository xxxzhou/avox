#pragma once

// ============================================================================
// 队友存活注册表: continuable 子代理的起动、冷恢复、中断与销毁。
//
// 对齐 dsh packages/experimental/agent-team/src/roster.ts (名册) +
// provider 的 startContinuable (起动) —— avox 的对应物是 Subagents.cpp 的子会话
// 创建序列, 但**不随工具栈帧拆除**: 队友会话跨工具调用存活, 直到销毁或进程重启。
//
// 三段式 spawn (与 dsh 的事务边界一致):
//   1. 事务: 校验 (名字/上限) + append provisioning 成员;
//   2. 锁外: 创建子会话 + followup 初始提示 + 等 checkpointInitialPrompt
//      (子日志出现首条 source=user 的 user/message, 或 turn 先收尾 = 拒绝);
//   3. 事务: settle —— 成功 append active, 失败 append failed(error) 并拆掉活驱动。
//   崩在段间: provisioning 残留, 恢复期 reconcileProvisioning 冷裁定。
//
// 线程模型: 一把 rosterMtx 只保护 liveMap 本身; 持它不得调用任何 agent 方法
// (cancel/send 都拿目标 agent 锁, 反向路径存在 —— 队友驱动线程上的 team 工具会取
// journalMtx)。锁序: journalMtx -> rosterMtx; rosterMtx 之后不得再取 journalMtx。
// ============================================================================

#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "avox_agent/core/Abort.hpp"
#include "avox_agent/core/Agent.hpp"
#include "avox_agent/core/ReactLoopAgent.hpp"
#include "avox_agent/core/SessionPersistence.hpp"
#include "avox_agent/core/SystemPrompt.hpp"
#include "avox_agent/core/ToolRuntime.hpp"
#include "TeamError.hpp"
#include "TeamJournal.hpp"
#include "TeamTypes.hpp"

namespace avox {

// 建员驱动依赖。team 域不 include compose 头 (compose -> team 单向), AgentHost 把
// 自己的设施经此借出 —— 与 Subagents 经 host getter 借出等价, 但不走类型依赖。
struct TeamRosterDeps {
  Agent* lead = nullptr;
  SystemPrompt* systemPrompt = nullptr;
  ToolRuntime* tools = nullptr;
  LlmProvider* llm = nullptr;
  AgentExtensionPoints* points = nullptr;
  std::string sessionRoot;
  std::optional<std::string> sessionCwd;
  int maxParallelToolCalls = 1;
  // 队友未显式指定 provider/model 时的地板 (Lead 的 options)。
  AgentOptions leadOptions;
  // 队友的委派深度: Lead 头行/选项深度的 max + 1, 落子会话头行。
  int memberDelegationDepth = 1;
};

// 一个活队友。writer 声明在前: 逆序析构时 agent 先停 (驱动线程 join), writer 后摘
// (它是 session 的观察者, 必须不先于会话关闭)。
struct TeamLiveMember {
  std::unique_ptr<SessionWriter> writer;
  std::unique_ptr<ReactLoopAgent> agent;
};

// 子日志冷校验的结论 (reconcile 与 ensureLive 共用)。
struct TeamChildLogCheck {
  bool ok = false;
  std::string error;
};

class TeamRoster {
 public:
  TeamRoster(TeamConfig config, TeamRosterDeps deps);

  TeamRoster(const TeamRoster&) = delete;
  TeamRoster& operator=(const TeamRoster&) = delete;

  // ---- spawn ----

  // 起动一个 continuable 队友。校验失败 (名字/上限) 抛 TeamError; 起动失败不抛 ——
  // 折叠成 phase=failed 的成员快照返回 (dsh: failed 是成员的正常终态, 不是异常)。
  // 成功返回 phase=active 的快照, 队友已在运行。
  TeamMemberSnapshot spawn(TeamJournal& journal,
                           const SpawnTeammateRequest& request);

  // ---- 存活管理 ----

  // 成员当前活驱动; 不在存活表返回 nullptr (Inactive)。
  ReactLoopAgent* findLive(const SessionId& memberId) const;

  // 确保一个 active 成员已冷恢复 (wakeup 投递 / 恢复期补投路径)。
  // 已活返回其驱动; 子日志缺失或校验不符抛 std::runtime_error (调用方决定语义:
  // 恢复期 -> failed, 投递期 -> 发送方可见的失败)。
  ReactLoopAgent& ensureLive(const TeamMemberSnapshot& member);

  // 中断成员的当前活动 (dsh interrupt: keepInbox —— 已排队工作保留)。
  // 目标非活成员抛 TeamError(TEAM_INVALID_TARGET)。
  InterruptResult interrupt(const TeamMemberSnapshot& member);

  // 折叠快照 + 存活表合成的运行时状态。
  TeamMemberRuntimeStatus statusOf(const TeamMemberSnapshot& member) const;

  // ---- 恢复期 ----

  // 折叠状态里仍 provisioning 的成员逐个冷裁定: 子日志齐备 -> active, 否则 failed。
  // **在 journal.transact 内调用** (TeamService 恢复流程); 只裁定不起动 ——
  // 恢复的成员保持 Inactive, 首条 wakeup 消息到达才 ensureLive (dsh 惰性)。
  void reconcileProvisioning(TeamJournal& journal);

  // ---- 销毁 ----

  // 停一个活成员: 摘出存活表 -> cancel(CancelByDisposed) -> whenIdle(预算) ->
  // shutdown (join) -> disposed 通知 -> writer 摘除。幂等; 不在存活表则空操作。
  void disposeMember(const SessionId& memberId, int64_t budgetMs);

  // 停全部活成员 (宿主关闭路径); 预算在剩余成员间均摊。
  void disposeAll(int64_t budgetMs);

  // 冷校验子日志 (头行血缘 / continuable 描述符 / provider 一致 / 初始提示在册)。
  // path 为空返回失败 —— 与文件不存在同因 (provisioning 崩在发布前)。
  static TeamChildLogCheck checkChildLog(const std::string& path,
                                         const SessionId& leadId,
                                         const TeamMemberSnapshot& member);

 private:
  // 冷恢复的公共实现: 读子日志 -> 校验 -> 建驱动 -> attach -> created 通知。
  // 失败抛 std::runtime_error。不投递任何输入。
  std::unique_ptr<TeamLiveMember> resumeFromDisk(const TeamMemberSnapshot& member);

  // spawn 段 2 的实现: 建子会话 (含 fork 种子/审批钉/描述符) + 驱动 + 落盘发布 +
  // followup 初始提示 + 等 checkpoint。返回未入注册表的活成员 (注册由 spawn 在
  // settle 前完成); 任何失败抛异常且不留活驱动。
  std::unique_ptr<TeamLiveMember> startContinuable(
      const TeamMemberSnapshot& snapshot, const SpawnTeammateRequest& request);

  TeamConfig config;
  TeamRosterDeps deps;

  mutable std::mutex rosterMtx;
  // key: member id 字符串。仅在此表里的成员有活驱动。
  std::map<std::string, TeamLiveMember> live;
};

}
