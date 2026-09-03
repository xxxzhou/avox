#pragma once

// ============================================================================
// Agent Teams 的公共身份、运行时视图与请求值。
//
// 对齐 dsh packages/experimental/agent-team/src/types.ts。
// 持久快照 (TeamMemberSnapshot / TeamTaskSnapshot / TeamMessageSnapshot) 直接
// 复用 core/SessionTypes.hpp 的定义 —— 它们就是 team/* 事件的 wire 载荷本体,
// 不做翻译层 (与 dsh「snapshot 接口即事件载荷」一致)。
// ============================================================================

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "avox_agent/core/Abort.hpp"
#include "avox_agent/core/SessionTypes.hpp"

namespace avox {

class Agent;

// 队伍部署限额 (dsh agent-team Config; 缺省值与 dsh index.ts 相同)。
struct TeamConfig {
  // 队伍保留的不可变队友名数上限。
  int maxMembers = 8;
  // 非删除态共享任务数上限。
  int maxTasks = 256;
  // 单个目标成员 queued 减 delivered 的上限。
  int maxPendingMessagesPerMember = 64;
  // 单条完整投递帧 (含框头) 的 UTF-8 字节上限。
  int maxMessageBytes = 65536;
  // 队伍销毁时等成员静止的预算 (毫秒)。
  int disposalTimeoutMs = 5000;
};

// 成员角色 (dsh: lead | teammate)。
enum class TeamRole { Lead, Teammate };

// 运行时 enriched 状态 (dsh TeamMemberView['status'])。
enum class TeamMemberRuntimeStatus {
  Running,
  Idle,
  Inactive,
  Provisioning,
  Failed,
};

// dsh TeamMemberView: 名册行, 带运行时状态与诊断。
struct TeamMemberView {
  SessionId id;
  std::string name;
  TeamRole role = TeamRole::Teammate;
  TeamMemberRuntimeStatus status = TeamMemberRuntimeStatus::Inactive;
  std::optional<std::string> description;
  std::optional<std::string> provider;
  std::optional<TeamMemberContext> context;
  std::optional<std::string> model;
  std::vector<std::string> diagnostics;
};

// dsh TeamTaskView: 任务行, 带 owner 名、就绪度与 write-scope 重叠警告。
struct TeamTaskView {
  std::string id;
  int revision = 1;
  std::string subject;
  std::string description;
  TeamTaskStatus status = TeamTaskStatus::Pending;
  std::vector<std::string> blockedBy;
  std::vector<std::string> writeScopes;
  std::optional<std::string> ownerName;
  bool ready = false;
  std::vector<std::string> writeScopeWarnings;
};

// dsh TeamMembership: 一次工具调用者的队伍归属 (Lead 或某个 active 队友)。
struct TeamMembership {
  // 只读身份 (消费者仅取 id / 判空), 故 const —— membershipOf 可在 const 上下文调用。
  const Agent* root = nullptr;
  TeamRole role = TeamRole::Lead;
  // 队伍内名字: Lead 恒 "lead", 队友为 spawn 时的名字。
  std::string name;
};

// spawn_teammate 请求。
struct SpawnTeammateRequest {
  std::string name;
  std::string description;
  std::vector<ContentBlock> prompt;
  TeamMemberContext context = TeamMemberContext::Fresh;
  // continuable provider 名 (avox: 'spawn' = fresh, 'fork' = fork 前缀)。
  std::string provider;
  std::shared_ptr<AbortSignal> signal;
};

// send_message / followup_task 请求。
struct SendTeamMessageRequest {
  std::string target;
  std::vector<ContentBlock> content;
  TeamMessageDelivery delivery = TeamMessageDelivery::Quiet;
  std::shared_ptr<AbortSignal> signal;
};

// 发送已入持久邮箱后的结果。
struct SendTeamMessageResult {
  std::string messageId;
  // accepted = 已投递到目标会话; queued = 持久排队 (目标不在/静默投递)。
  bool accepted = false;
};

// interrupt_agent 结果。
struct InterruptResult {
  TeamMemberRuntimeStatus previousStatus = TeamMemberRuntimeStatus::Inactive;
};

// 共享任务创建请求。
struct CreateTeamTaskRequest {
  std::string subject;
  std::string description;
  std::optional<std::vector<std::string>> blockedBy;
  std::optional<std::vector<std::string>> writeScopes;
};

// 共享任务 CAS 变更动作 (dsh TeamTaskAction)。
enum class TeamTaskAction {
  Claim,
  Release,
  Edit,
  SetDependencies,
  Complete,
  Reopen,
  Reassign,
  Delete,
};

// 共享任务 CAS 变更请求。
struct UpdateTeamTaskRequest {
  std::string taskId;
  int expectedRevision = 0;
  TeamTaskAction action = TeamTaskAction::Claim;
  std::optional<std::string> subject;
  std::optional<std::string> description;
  std::optional<std::vector<std::string>> blockedBy;
  std::optional<std::vector<std::string>> writeScopes;
  // reassign 专用: 目标成员名; 空串 = 取消归属。
  std::optional<std::string> owner;
};

// wait_agent 结果。
struct TeamWaitResult {
  bool timedOut = false;
};

}
