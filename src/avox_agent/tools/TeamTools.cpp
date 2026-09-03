#include "TeamTools.hpp"

#include <functional>
#include <string>
#include <utility>

#include "avox_agent/compose/AgentHost.hpp"
#include "ToolArgs.hpp"
#include "avox/module/Json.hpp"
#include "avox_agent/core/Agent.hpp"
#include "avox_agent/core/SystemPrompt.hpp"
#include "avox_agent/core/ToolRuntime.hpp"
#include "avox_agent/team/TeamError.hpp"
#include "avox_agent/team/TeamService.hpp"
#include "avox_agent/team/TeamValidation.hpp"

namespace avox {

namespace {

// dsh wait 的合法区间 (工具入口自查; 分片等待不再经 activity.wait 的校验)。
constexpr int64_t kMinWaitMs = 10000;
constexpr int64_t kMaxWaitMs = 3600000;

// 服务未就绪 (会话未开/未启用) 的统一错误。
ToolResult teamUnavailable() {
  return toolError(ToolOutcome::Fatal, "team is not enabled for this session",
                   "TEAM_MEMBER_NOT_FOUND");
}

// 队伍操作的统一出口: op 产出结果; TeamError 携稳定码, 其余异常收敛成内部码。
ToolResult teamOp(const std::function<ToolResult()>& op) {
  try {
    return op();
  } catch (const TeamError& e) {
    return toolError(ToolOutcome::Fatal, e.what(), e.code);
  } catch (const std::exception& e) {
    return toolError(ToolOutcome::Fatal, e.what(), "TEAM_INTERNAL");
  }
}

// ---- 视图 -> JSON 文本 (手工拼, jsonEscape 做转义) ----

const char* memberStatusName(TeamMemberRuntimeStatus status) {
  switch (status) {
    case TeamMemberRuntimeStatus::Running: return "running";
    case TeamMemberRuntimeStatus::Idle: return "idle";
    case TeamMemberRuntimeStatus::Inactive: return "inactive";
    case TeamMemberRuntimeStatus::Provisioning: return "provisioning";
    case TeamMemberRuntimeStatus::Failed: return "failed";
  }
  return "inactive";
}

const char* taskStatusName(TeamTaskStatus status) {
  switch (status) {
    case TeamTaskStatus::Pending: return "pending";
    case TeamTaskStatus::InProgress: return "in_progress";
    case TeamTaskStatus::Completed: return "completed";
    case TeamTaskStatus::Deleted: return "deleted";
  }
  return "pending";
}

std::string jsonArrayOf(const std::vector<std::string>& values) {
  std::string json = "[";
  for (size_t i = 0; i < values.size(); ++i) {
    if (i > 0) json += ",";
    json += "\"" + jsonEscape(values[i]) + "\"";
  }
  json += "]";
  return json;
}

std::string memberJson(const TeamMemberView& view) {
  std::string json = "{";
  json += "\"id\":\"" + jsonEscape(view.id.value) + "\"";
  json += ",\"name\":\"" + jsonEscape(view.name) + "\"";
  json += view.role == TeamRole::Lead ? ",\"role\":\"lead\""
                                      : ",\"role\":\"teammate\"";
  json += ",\"status\":\"" + std::string(memberStatusName(view.status)) + "\"";
  if (view.description.has_value()) {
    json += ",\"description\":\"" + jsonEscape(*view.description) + "\"";
  }
  if (view.provider.has_value() && !view.provider->empty()) {
    json += ",\"provider\":\"" + jsonEscape(*view.provider) + "\"";
  }
  if (view.model.has_value() && !view.model->empty()) {
    json += ",\"model\":\"" + jsonEscape(*view.model) + "\"";
  }
  if (view.context.has_value()) {
    json += *view.context == TeamMemberContext::Fork
                ? ",\"context\":\"fork\""
                : ",\"context\":\"fresh\"";
  }
  if (!view.diagnostics.empty()) {
    json += ",\"diagnostics\":" + jsonArrayOf(view.diagnostics);
  }
  json += "}";
  return json;
}

std::string taskJson(const TeamTaskView& view) {
  std::string json = "{";
  json += "\"id\":\"" + jsonEscape(view.id) + "\"";
  json += ",\"revision\":" + std::to_string(view.revision);
  json += ",\"subject\":\"" + jsonEscape(view.subject) + "\"";
  json += ",\"description\":\"" + jsonEscape(view.description) + "\"";
  json += ",\"status\":\"" + std::string(taskStatusName(view.status)) + "\"";
  if (view.ownerName.has_value()) {
    json += ",\"owner\":\"" + jsonEscape(*view.ownerName) + "\"";
  }
  json += ",\"blockedBy\":" + jsonArrayOf(view.blockedBy);
  json += ",\"writeScopes\":" + jsonArrayOf(view.writeScopes);
  json += view.ready ? ",\"ready\":true" : ",\"ready\":false";
  if (!view.writeScopeWarnings.empty()) {
    json += ",\"writeScopeWarnings\":" + jsonArrayOf(view.writeScopeWarnings);
  }
  json += "}";
  return json;
}

std::string taskJsonOf(const TeamTaskSnapshot& task) {
  TeamTaskView view;
  view.id = task.id;
  view.revision = task.revision;
  view.subject = task.subject;
  view.description = task.description;
  view.status = task.status;
  view.blockedBy = task.blockedBy;
  view.writeScopes = task.writeScopes;
  return taskJson(view);
}

// ---- team:policy 提示段 (order 60, dsh 同位) ----

// 按装配对象渲染视角; 非成员 (one-shot 子代理) 或未开队伍渲染空串 —— 空段被
// SystemPrompt::assemble 剔除, 不占提示词。
std::string policyText(AgentHost& host, const AssembleContext& context) {
  TeamService* team = host.team();
  if (team == nullptr || context.agent == nullptr) return std::string();
  TeamMembership membership;
  try {
    membership = team->membershipOf(context.agent);
  } catch (const TeamError&) {
    return std::string();
  }
  std::string text = "## Team policy\n\n";
  if (membership.role == TeamRole::Lead) {
    text += "You are the lead of a durable agent team. The roster, shared "
            "tasks, and mailboxes live in this session's log and survive "
            "restarts. Members address you as \"lead\".\n\n";
  } else {
    text += "You are teammate \"" + membership.name + "\" on a durable agent "
            "team led by \"lead\". Your permission scope was fixed when you "
            "were started: operations that require approval are rejected "
            "automatically — state the limitation in your reply instead of "
            "retrying.\n\n";
  }
  text += "- spawn_teammate: start a named teammate (lowercase letters, "
          "digits, single dashes). The prompt must be self-contained; the "
          "teammate runs in its own session and stays available for "
          "follow-ups.\n"
          "- send_message: deliver a message to \"lead\" or a teammate "
          "without waking an idle member; followup_task wakes the target "
          "(an inactive teammate is resumed from its own session log).\n"
          "- list_agents / wait_agent: inspect the roster and wait for the "
          "next team change (10000-3600000 ms).\n"
          "- interrupt_agent: cancel a member's current activity; queued "
          "work is kept.\n"
          "- team_task_create / team_task_list / team_task_get / "
          "team_task_update: shared task board. Updates are compare-and-swap "
          "on revision — reread a task after a stale-revision error; claim "
          "only ready tasks; complete what you own.\n"
          "- Delivered messages arrive framed as \"Team message <id> from "
          "<sender>: ...\"; quote the id when referring to one.";
  return text;
}

}  // namespace

std::vector<Disposer> installTeamTools(AgentHost& host) {
  std::vector<Disposer> disposers;
  const auto define = [&host, &disposers](ToolDefinition definition) {
    disposers.push_back(host.defineTool(std::move(definition)));
  };

  // ---- 队友 ----

  define(ToolDefinition{
      "spawn_teammate",
      "Start a named continuable teammate with its own session. The prompt "
      "must be self-contained (the teammate sees no other session). The "
      "teammate stays alive for follow-ups; spawn failure is reported as an "
      "error with the member's diagnostics.",
      R"({"type":"object","properties":{"name":{"type":"string","description":"lowercase-kebab member name, unique forever"},"description":{"type":"string","description":"one-line role summary shown in list_agents"},"prompt":{"type":"string","description":"self-contained initial task message"},"context":{"type":"string","enum":["fresh","fork"],"description":"fresh = empty session; fork = inherit this session's history up to the last completed turn"},"provider":{"type":"string","description":"provider override for the teammate; default: this session's provider"}},"required":["name","description","prompt"]})",
      [&host](const ToolExecution& exec) -> ToolResult {
        TeamService* team = host.team();
        if (team == nullptr) return teamUnavailable();
        const Json args = parseToolArgs(exec.argumentsJson);
        SpawnTeammateRequest request;
        request.name = toolArgString(args, "name");
        request.description = toolArgString(args, "description");
        request.prompt = {ContentBlock{
            TextBlock{toolArgString(args, "prompt")}}};
        const std::string context = toolArgString(args, "context", "fresh");
        if (context == "fork") {
          request.context = TeamMemberContext::Fork;
        } else if (context != "fresh") {
          return toolError(ToolOutcome::Fatal,
                           "context must be \"fresh\" or \"fork\"",
                           "TEAM_INVALID_ARGUMENT");
        }
        request.provider = toolArgString(args, "provider");
        request.signal = exec.signal;
        return teamOp([&]() -> ToolResult {
          const TeamMemberSnapshot member = team->spawnTeammate(request);
          if (member.phase != TeamMemberPhase::Active) {
            throw TeamError("teammate \"" + member.name
                                + "\" failed to start: "
                                + member.error.value_or("unknown error"),
                            "TEAM_MEMBER_FAILED");
          }
          return toolOk("Started teammate \"" + member.name + "\" ("
                        + member.id.value + ", " + member.provider + ").");
        });
      }});

  // send_message (默认 quiet) / followup_task (wakeup) —— 同一发送路径, 两个预设。
  const auto makeSender = [&host](const char* name, const char* description,
                                  bool wakeup) -> ToolDefinition {
    return ToolDefinition{
        name,
        description,
        wakeup
            ? R"({"type":"object","properties":{"target":{"type":"string","description":"\"lead\" or a teammate name"},"message":{"type":"string","description":"message body"}},"required":["target","message"]})"
            : R"({"type":"object","properties":{"target":{"type":"string","description":"\"lead\" or a teammate name"},"message":{"type":"string","description":"message body"},"delivery":{"type":"string","enum":["quiet","wakeup"],"description":"quiet = do not wake an idle target (default); wakeup = start a turn"}},"required":["target","message"]})",
        [&host, wakeup](const ToolExecution& exec) -> ToolResult {
          TeamService* team = host.team();
          if (team == nullptr) return teamUnavailable();
          const Json args = parseToolArgs(exec.argumentsJson);
          SendTeamMessageRequest request;
          request.target = toolArgString(args, "target");
          request.content = {ContentBlock{
              TextBlock{toolArgString(args, "message")}}};
          const std::string delivery =
              toolArgString(args, "delivery", wakeup ? "wakeup" : "quiet");
          if (delivery == "wakeup") {
            request.delivery = TeamMessageDelivery::Wakeup;
          } else if (delivery == "quiet") {
            request.delivery = TeamMessageDelivery::Quiet;
          } else {
            return toolError(ToolOutcome::Fatal,
                             "delivery must be \"quiet\" or \"wakeup\"",
                             "TEAM_INVALID_ARGUMENT");
          }
          request.signal = exec.signal;
          return teamOp([&]() -> ToolResult {
            const SendTeamMessageResult result =
                team->sendMessage(*exec.agent, request);
            if (result.accepted) {
              return toolOk("Message " + result.messageId + " delivered to "
                            + request.target + ".");
            }
            return toolOk("Message " + result.messageId + " queued for "
                          + request.target
                          + " (delivered when the member wakes).");
          });
        }};
  };
  define(makeSender(
      "send_message",
      "Deliver a message to \"lead\" or a teammate. Quiet delivery (default) "
      "lands in the target's persistent inbox without waking an idle member.",
      false));
  define(makeSender(
      "followup_task",
      "Send a message that wakes its target: starts a turn in a running "
      "member, or resumes an inactive teammate from its session log first.",
      true));

  define(ToolDefinition{
      "list_agents",
      "List the team: the lead plus every teammate with runtime status.",
      R"({"type":"object","properties":{}})",
      [&host](const ToolExecution&) -> ToolResult {
        TeamService* team = host.team();
        if (team == nullptr) return teamUnavailable();
        std::string json = "[";
        for (const TeamMemberView& view : team->listMembers()) {
          if (json.size() > 1) json += ",";
          json += memberJson(view);
        }
        json += "]";
        return toolOk(json);
      }});

  define(ToolDefinition{
      "wait_agent",
      "Block until the next team change (member/task/mailbox event) or the "
      "timeout (10000-3600000 ms). Use to wait for teammate replies.",
      R"({"type":"object","properties":{"timeoutMs":{"type":"integer","description":"wait budget in milliseconds; range 10000-3600000, default 30000"}}})",
      [&host](const ToolExecution& exec) -> ToolResult {
        TeamService* team = host.team();
        if (team == nullptr) return teamUnavailable();
        const Json args = parseToolArgs(exec.argumentsJson);
        const int64_t timeoutMs = toolArgInt(args, "timeoutMs", 30000);
        if (timeoutMs < kMinWaitMs || timeoutMs > kMaxWaitMs) {
          return toolError(ToolOutcome::Fatal,
                           "timeoutMs must be between 10000 and 3600000",
                           "TEAM_INVALID_TIMEOUT");
        }
        return teamOp([&]() -> ToolResult {
          // 分片轮询: 保留整体预算与变更代数语义, 对取消信号保持响应。
          constexpr int64_t kSliceMs = 250;
          int64_t remaining = timeoutMs;
          while (remaining > 0) {
            if (exec.signal != nullptr && exec.signal->aborted()) {
              return toolError(ToolOutcome::Aborted, "wait_agent aborted",
                               TOOL_CODE_ABORTED);
            }
            const int64_t slice = remaining < kSliceMs ? remaining : kSliceMs;
            const TeamWaitResult result = team->pollForChange(slice);
            if (!result.timedOut) {
              return toolOk("Team changed.");
            }
            remaining -= slice;
          }
          return toolOk("Timed out with no team change.");
        });
      },
      // 协作式预算: 覆盖最长合法等待 (1h) 加一片余量。
      3601000});

  define(ToolDefinition{
      "interrupt_agent",
      "Cancel a teammate's current activity. Queued work is kept; the "
      "teammate stays on the roster.",
      R"({"type":"object","properties":{"target":{"type":"string","description":"teammate name"}},"required":["target"]})",
      [&host](const ToolExecution& exec) -> ToolResult {
        TeamService* team = host.team();
        if (team == nullptr) return teamUnavailable();
        const Json args = parseToolArgs(exec.argumentsJson);
        const std::string target = toolArgString(args, "target");
        return teamOp([&]() -> ToolResult {
          const TeamMembership membership = team->membershipOf(exec.agent);
          const InterruptResult result =
              team->interruptAgent(membership, target);
          return toolOk(std::string("Interrupted ") + target + " (was "
                        + memberStatusName(result.previousStatus)
                        + "). Queued work kept.");
        });
      }});

  // ---- 共享任务板 ----

  define(ToolDefinition{
      "team_task_create",
      "Create a shared task on the team board.",
      R"({"type":"object","properties":{"subject":{"type":"string"},"description":{"type":"string"},"blockedBy":{"type":"array","items":{"type":"string"},"description":"task ids this task waits on"},"writeScopes":{"type":"array","items":{"type":"string"},"description":"member names allowed to modify/claim this task; empty = unrestricted, the lead is always allowed"}},"required":["subject"]})",
      [&host](const ToolExecution& exec) -> ToolResult {
        TeamService* team = host.team();
        if (team == nullptr) return teamUnavailable();
        const Json args = parseToolArgs(exec.argumentsJson);
        CreateTeamTaskRequest request;
        request.subject = toolArgString(args, "subject");
        request.description = toolArgString(args, "description");
        if (args.bObject() && args.find("blockedBy")) {
          request.blockedBy = toolArgStringArray(args, "blockedBy");
        }
        if (args.bObject() && args.find("writeScopes")) {
          // 逐个规范化 (反斜杠/前缀清理); 非法项响亮拒绝, 存的是规范形式。
          std::vector<std::string> scopes;
          for (const std::string& scope : toolArgStringArray(args, "writeScopes")) {
            scopes.push_back(teamWriteScope(scope));
          }
          request.writeScopes = std::move(scopes);
        }
        return teamOp([&]() -> ToolResult {
          const TeamTaskSnapshot task = team->createTask(request);
          return toolOk("Created " + task.id + " revision "
                        + std::to_string(task.revision) + ": \""
                        + task.subject + "\"");
        });
      }});

  define(ToolDefinition{
      "team_task_list",
      "List non-deleted tasks with owner, readiness and write-scope warnings.",
      R"({"type":"object","properties":{}})",
      [&host](const ToolExecution&) -> ToolResult {
        TeamService* team = host.team();
        if (team == nullptr) return teamUnavailable();
        std::string json = "[";
        for (const TeamTaskView& view : team->listTasks()) {
          if (json.size() > 1) json += ",";
          json += taskJson(view);
        }
        json += "]";
        return toolOk(json);
      }});

  define(ToolDefinition{
      "team_task_get",
      "Read one task by id.",
      R"({"type":"object","properties":{"taskId":{"type":"string"}},"required":["taskId"]})",
      [&host](const ToolExecution& exec) -> ToolResult {
        TeamService* team = host.team();
        if (team == nullptr) return teamUnavailable();
        const Json args = parseToolArgs(exec.argumentsJson);
        const std::string taskId = toolArgString(args, "taskId");
        return teamOp([&]() -> ToolResult {
          return toolOk(taskJsonOf(team->getTask(taskId)));
        });
      }});

  define(ToolDefinition{
      "team_task_update",
      "Compare-and-swap task change. Reread the task after a stale-revision "
      "error.",
      R"({"type":"object","properties":{"taskId":{"type":"string"},"expectedRevision":{"type":"integer"},"action":{"type":"string","enum":["claim","release","edit","set_dependencies","complete","reopen","reassign","delete"]},"subject":{"type":"string"},"description":{"type":"string"},"blockedBy":{"type":"array","items":{"type":"string"}},"writeScopes":{"type":"array","items":{"type":"string"}},"owner":{"type":"string","description":"reassign target member name; empty string clears ownership"}},"required":["taskId","expectedRevision","action"]})",
      [&host](const ToolExecution& exec) -> ToolResult {
        TeamService* team = host.team();
        if (team == nullptr) return teamUnavailable();
        const Json args = parseToolArgs(exec.argumentsJson);
        UpdateTeamTaskRequest request;
        request.taskId = toolArgString(args, "taskId");
        request.expectedRevision = toolArgInt(args, "expectedRevision", -1);
        const std::string action = toolArgString(args, "action");
        if (action == "claim") {
          request.action = TeamTaskAction::Claim;
        } else if (action == "release") {
          request.action = TeamTaskAction::Release;
        } else if (action == "edit") {
          request.action = TeamTaskAction::Edit;
        } else if (action == "set_dependencies") {
          request.action = TeamTaskAction::SetDependencies;
        } else if (action == "complete") {
          request.action = TeamTaskAction::Complete;
        } else if (action == "reopen") {
          request.action = TeamTaskAction::Reopen;
        } else if (action == "reassign") {
          request.action = TeamTaskAction::Reassign;
        } else if (action == "delete") {
          request.action = TeamTaskAction::Delete;
        } else {
          return toolError(ToolOutcome::Fatal,
                           "unknown task action \"" + action + "\"",
                           "TEAM_INVALID_ARGUMENT");
        }
        if (args.bObject() && args.find("subject")) {
          request.subject = toolArgString(args, "subject");
        }
        if (args.bObject() && args.find("description")) {
          request.description = toolArgString(args, "description");
        }
        if (args.bObject() && args.find("blockedBy")) {
          request.blockedBy = toolArgStringArray(args, "blockedBy");
        }
        if (args.bObject() && args.find("writeScopes")) {
          std::vector<std::string> scopes;
          for (const std::string& scope : toolArgStringArray(args, "writeScopes")) {
            scopes.push_back(teamWriteScope(scope));
          }
          request.writeScopes = std::move(scopes);
        }
        if (args.bObject() && args.find("owner")) {
          request.owner = toolArgString(args, "owner");
        }
        return teamOp([&]() -> ToolResult {
          const TeamMembership membership = team->membershipOf(exec.agent);
          const TeamTaskSnapshot task =
              team->updateTask(membership, request);
          return toolOk("Updated " + task.id + " to revision "
                        + std::to_string(task.revision) + " (" + action
                        + ", " + taskStatusName(task.status) + ")");
        });
      }});

  // ---- 提示段 ----

  disposers.push_back(host.prompt().section(
      PromptSection{"team:policy", 60,
                    [&host](const AssembleContext& context) {
                      return policyText(host, context);
                    }}));
  return disposers;
}

}
