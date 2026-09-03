#include "TeamService.hpp"

#include <utility>

#include "avox/module/LogHelper.hpp"

namespace avox {

TeamService::TeamService(TeamConfig configValue, TeamRosterDeps deps,
                         SessionWriter& leadWriter)
    : teamConfig(configValue),
      lead(*deps.lead),
      teamJournal(*deps.lead, leadWriter, activity),
      teamRoster(configValue, std::move(deps)),
      mailbox(*deps.lead, configValue),
      taskBoard(*deps.lead, configValue) {}

TeamMembership TeamService::membershipOf(const Agent* caller) const {
  TeamMembership membership;
  if (caller == nullptr) {
    throw TeamError("calling agent is required", TEAM_INVALID_ARGUMENT);
  }
  if (caller == &lead) {
    membership.root = caller;
    membership.role = TeamRole::Lead;
    membership.name = "lead";
    return membership;
  }
  const SessionId& callerId = caller->id();
  teamJournal.readState([&](const TeamFoldState& state) {
    const TeamMemberSnapshot* member = state.memberById(callerId);
    if (member != nullptr && member->phase == TeamMemberPhase::Active) {
      membership.root = caller;
      membership.role = TeamRole::Teammate;
      membership.name = member->name;
    }
  });
  if (membership.root == nullptr) {
    throw TeamError("calling agent \"" + callerId.value
                        + "\" is not part of this team",
                    TEAM_MEMBER_NOT_FOUND);
  }
  return membership;
}

const TeamMemberSnapshot* TeamService::memberForTarget(
    const std::string& target) const {
  const TeamMemberSnapshot* resolved = nullptr;
  teamJournal.readState([&](const TeamFoldState& state) {
    resolved = state.memberByName(target);
    if (resolved == nullptr) {
      for (const TeamMemberSnapshot& member : state.members) {
        if (member.id.value == target) {
          resolved = &member;
          break;
        }
      }
    }
  });
  if (resolved == nullptr) {
    throw TeamError("team target \"" + target + "\" does not exist",
                    TEAM_INVALID_TARGET);
  }
  return resolved;
}

TeamMemberSnapshot TeamService::spawnTeammate(
    const SpawnTeammateRequest& request) {
  return teamRoster.spawn(teamJournal, request);
}

std::vector<TeamMemberView> TeamService::listMembers() const {
  std::vector<TeamMemberView> views;
  // Lead 行: 恒在, 恒活 (宿主主会话)。
  {
    TeamMemberView leadView;
    leadView.id = lead.id();
    leadView.name = "lead";
    leadView.role = TeamRole::Lead;
    leadView.status = lead.status() == AgentStatus::Running
                          ? TeamMemberRuntimeStatus::Running
                          : TeamMemberRuntimeStatus::Idle;
    leadView.provider = lead.options().provider;
    views.push_back(std::move(leadView));
  }
  teamJournal.readState([&](const TeamFoldState& state) {
    for (const TeamMemberSnapshot& member : state.members) {
      TeamMemberView view;
      view.id = member.id;
      view.name = member.name;
      view.role = TeamRole::Teammate;
      view.status = teamRoster.statusOf(member);
      if (!member.description.empty()) view.description = member.description;
      view.provider = member.provider;
      view.context = member.context;
      if (member.error.has_value()) {
        view.diagnostics.push_back(*member.error);
      }
      views.push_back(std::move(view));
    }
  });
  return views;
}

InterruptResult TeamService::interruptAgent(const TeamMembership& membership,
                                            const std::string& target) {
  if (target == "lead" || target == lead.id().value) {
    throw TeamError("the lead cannot be interrupted from a team tool",
                    TEAM_LEAD_REQUIRED);
  }
  (void)membership;
  const TeamMemberSnapshot* member = memberForTarget(target);
  if (member->phase != TeamMemberPhase::Active) {
    throw TeamError("member \"" + member->name + "\" is not active",
                    TEAM_INVALID_TARGET);
  }
  return teamRoster.interrupt(*member);
}

SendTeamMessageResult TeamService::sendMessage(
    const Agent& sender, const SendTeamMessageRequest& request) {
  const TeamMembership membership = membershipOf(&sender);
  return mailbox.send(teamJournal, teamRoster, sender, membership.name,
                      request);
}

TeamTaskSnapshot TeamService::createTask(const CreateTeamTaskRequest& request) {
  return taskBoard.create(teamJournal, request);
}

TeamTaskSnapshot TeamService::getTask(const std::string& taskId) const {
  return taskBoard.get(teamJournal, taskId);
}

std::vector<TeamTaskView> TeamService::listTasks() const {
  return taskBoard.list(teamJournal);
}

TeamTaskSnapshot TeamService::updateTask(const TeamMembership& membership,
                                         const UpdateTeamTaskRequest& request) {
  return taskBoard.update(teamJournal, membership, request);
}

TeamWaitResult TeamService::waitForChange(int64_t timeoutMs) {
  return activity.wait(timeoutMs);
}

TeamWaitResult TeamService::pollForChange(int64_t maxMs) {
  return activity.poll(maxMs);
}

void TeamService::recover() {
  teamJournal.transact([&]() {
    teamJournal.reloadFromLead();
    teamRoster.reconcileProvisioning(teamJournal);
    mailbox.dispatchPending(teamJournal, teamRoster);
  });
  LOGFLF(LogLevel::info, "[team] 恢复完成 (重折叠 + provisioning 裁定 + 邮箱补投)");
}

void TeamService::dispose() {
  activity.close();
  teamRoster.disposeAll(teamConfig.disposalTimeoutMs);
}

}
