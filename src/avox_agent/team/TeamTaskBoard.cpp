#include "TeamTaskBoard.hpp"

#include <stdexcept>
#include <utility>

#include "avox_agent/core/Agent.hpp"

namespace avox {

namespace {

// 逗号拼接 (错误消息用)。
std::string joinNames(const std::vector<std::string>& names) {
  std::string joined;
  for (const std::string& name : names) {
    if (!joined.empty()) joined += ", ";
    joined += name;
  }
  return joined;
}

// writeScopes 准入: 名单非空时仅名单内队友 (Lead 豁免) 可变更。
void checkWriteScope(const TeamMembership& membership,
                     const TeamTaskSnapshot& task) {
  if (membership.role == TeamRole::Lead) return;
  if (task.writeScopes.empty()) return;
  for (const std::string& scope : task.writeScopes) {
    if (scope == membership.name) return;
  }
  throw TeamError("task \"" + task.id + "\" is limited to write scopes ["
                      + joinNames(task.writeScopes) + "]",
                  TEAM_TASK_UNAUTHORIZED);
}

}  // namespace

TeamTaskBoard::TeamTaskBoard(Agent& leadAgent, TeamConfig configValue)
    : lead(leadAgent), config(configValue) {}

std::map<std::string, TeamTaskSnapshot> TeamTaskBoard::taskMapOf(
    const TeamFoldState& state) {
  std::map<std::string, TeamTaskSnapshot> tasks;
  for (const TeamTaskSnapshot& task : state.tasks) {
    tasks.emplace(task.id, task);
  }
  return tasks;
}

TeamError TeamTaskBoard::mapGraphError(const TeamTaskGraphError& e) {
  switch (e.violation) {
    case TeamTaskGraphViolation::Missing:
      return TeamError(e.what(), TEAM_TASK_NOT_FOUND);
    case TeamTaskGraphViolation::Duplicate:
      return TeamError(e.what(), TEAM_INVALID_ARGUMENT);
    case TeamTaskGraphViolation::Cycle:
      return TeamError(e.what(), TEAM_TASK_DEPENDENCY_CYCLE);
  }
  return TeamError(e.what(), TEAM_INVALID_ARGUMENT);
}

TeamTaskSnapshot TeamTaskBoard::create(TeamJournal& journal,
                                       const CreateTeamTaskRequest& request) {
  if (request.subject.empty()) {
    throw TeamError("task subject must not be empty", TEAM_INVALID_ARGUMENT);
  }
  TeamTaskSnapshot created;
  journal.transact([&]() {
    TeamFoldState& state = journal.state();
    size_t active = 0;
    for (const TeamTaskSnapshot& task : state.tasks) {
      if (task.status != TeamTaskStatus::Deleted) ++active;
    }
    if (active >= static_cast<size_t>(config.maxTasks)) {
      throw TeamError("team task limit reached ("
                          + std::to_string(config.maxTasks) + ")",
                      TEAM_TASK_LIMIT);
    }
    created.id = "task-" + std::to_string(state.nextTaskNumber);
    created.revision = 1;
    created.subject = request.subject;
    created.description = request.description;
    created.status = TeamTaskStatus::Pending;
    if (request.blockedBy.has_value()) created.blockedBy = *request.blockedBy;
    if (request.writeScopes.has_value()) {
      created.writeScopes = *request.writeScopes;
    }
    try {
      assertTeamTaskGraphCandidate(taskMapOf(state), created);
    } catch (const TeamTaskGraphError& e) {
      throw mapGraphError(e);
    }
    journal.appendAndFlush(
        TeamTaskEventData{TEAM_EVENT_VERSION, lead.id(), created});
  });
  return created;
}

TeamTaskSnapshot TeamTaskBoard::update(TeamJournal& journal,
                                       const TeamMembership& membership,
                                       const UpdateTeamTaskRequest& request) {
  TeamTaskSnapshot updated;
  journal.transact([&]() {
    TeamFoldState& state = journal.state();
    const TeamTaskSnapshot* existing = state.taskById(request.taskId);
    if (existing == nullptr) {
      throw TeamError("team task \"" + request.taskId + "\" does not exist",
                      TEAM_TASK_NOT_FOUND);
    }
    if (existing->status == TeamTaskStatus::Deleted) {
      throw TeamError("team task \"" + request.taskId + "\" is deleted",
                      TEAM_TASK_DELETED);
    }
    if (request.expectedRevision != existing->revision) {
      throw TeamError("team task \"" + request.taskId + "\" revision "
                          + std::to_string(existing->revision)
                          + " does not match expected "
                          + std::to_string(request.expectedRevision),
                      TEAM_TASK_STALE_REVISION);
    }
    checkWriteScope(membership, *existing);
    updated = *existing;
    ++updated.revision;

    // blockedBy 引用现存 (非删除) 任务的判定, 多处复用。
    const auto checkBlockers = [&](const std::vector<std::string>& blockedBy) {
      for (const std::string& blocker : blockedBy) {
        const TeamTaskSnapshot* ref = state.taskById(blocker);
        if (ref == nullptr) {
          throw TeamError("blocked-by task \"" + blocker + "\" does not exist",
                          TEAM_TASK_NOT_FOUND);
        }
        if (ref->status == TeamTaskStatus::Deleted) {
          throw TeamError("blocked-by task \"" + blocker + "\" is deleted",
                          TEAM_TASK_DELETED);
        }
      }
    };
    // 未完成依赖清单 (TEAM_TASK_BLOCKED 的载荷)。
    const auto outstandingBlockers = [&](const std::vector<std::string>& blockedBy) {
      std::string outstanding;
      for (const std::string& blocker : blockedBy) {
        const TeamTaskSnapshot* ref = state.taskById(blocker);
        if (ref == nullptr || ref->status == TeamTaskStatus::Deleted) continue;
        if (ref->status == TeamTaskStatus::Completed) continue;
        if (!outstanding.empty()) outstanding += ", ";
        outstanding += blocker;
      }
      return outstanding;
    };

    switch (request.action) {
      case TeamTaskAction::Claim: {
        // 有主优先于状态报 ALREADY_CLAIMED: 重复 claim 是最常见竞争, 给模型
        // 更精确的自愈信号 (not pending 留给无主但状态不对的任务)。
        if (existing->ownerId.has_value()) {
          throw TeamError("team task \"" + existing->id + "\" is already claimed",
                          TEAM_TASK_ALREADY_CLAIMED);
        }
        if (existing->status != TeamTaskStatus::Pending) {
          throw TeamError("team task \"" + existing->id + "\" is not pending",
                          TEAM_TASK_INVALID_TRANSITION);
        }
        const std::string outstanding = outstandingBlockers(existing->blockedBy);
        if (!outstanding.empty()) {
          throw TeamError("team task \"" + existing->id + "\" is blocked by "
                              + outstanding,
                          TEAM_TASK_BLOCKED);
        }
        updated.ownerId = membership.root->id();
        updated.status = TeamTaskStatus::InProgress;
        break;
      }
      case TeamTaskAction::Release: {
        if (existing->status != TeamTaskStatus::InProgress) {
          throw TeamError("team task \"" + existing->id
                              + "\" is not in progress",
                          TEAM_TASK_INVALID_TRANSITION);
        }
        if (membership.role != TeamRole::Lead
            && !(existing->ownerId == membership.root->id())) {
          throw TeamError("team task \"" + existing->id
                              + "\" is owned by another member",
                          TEAM_TASK_UNAUTHORIZED);
        }
        updated.ownerId.reset();
        updated.status = TeamTaskStatus::Pending;
        break;
      }
      case TeamTaskAction::Edit: {
        if (request.subject.has_value()) updated.subject = *request.subject;
        if (request.description.has_value()) {
          updated.description = *request.description;
        }
        break;
      }
      case TeamTaskAction::SetDependencies: {
        if (!request.blockedBy.has_value()) {
          throw TeamError("set_dependencies requires blockedBy",
                          TEAM_INVALID_ARGUMENT);
        }
        updated.blockedBy = *request.blockedBy;
        checkBlockers(updated.blockedBy);
        try {
          assertTeamTaskGraphCandidate(taskMapOf(state), updated);
        } catch (const TeamTaskGraphError& e) {
          throw mapGraphError(e);
        }
        break;
      }
      case TeamTaskAction::Complete: {
        if (existing->status != TeamTaskStatus::InProgress) {
          throw TeamError("team task \"" + existing->id
                              + "\" is not in progress",
                          TEAM_TASK_INVALID_TRANSITION);
        }
        if (membership.role != TeamRole::Lead
            && !(existing->ownerId == membership.root->id())) {
          throw TeamError("team task \"" + existing->id
                              + "\" is owned by another member",
                          TEAM_TASK_UNAUTHORIZED);
        }
        updated.status = TeamTaskStatus::Completed;
        break;
      }
      case TeamTaskAction::Reopen: {
        if (existing->status != TeamTaskStatus::Completed) {
          throw TeamError("team task \"" + existing->id + "\" is not completed",
                          TEAM_TASK_INVALID_TRANSITION);
        }
        updated.status = TeamTaskStatus::Pending;
        updated.ownerId.reset();
        break;
      }
      case TeamTaskAction::Reassign: {
        if (!request.owner.has_value()) {
          throw TeamError("reassign requires owner", TEAM_INVALID_ARGUMENT);
        }
        if (request.owner->empty()) {
          updated.ownerId.reset();
          break;
        }
        const TeamMemberSnapshot* member = state.memberByName(*request.owner);
        if (member == nullptr || member->phase != TeamMemberPhase::Active) {
          throw TeamError("reassign target \"" + *request.owner
                              + "\" is not an active member",
                          TEAM_MEMBER_NOT_FOUND);
        }
        updated.ownerId = member->id;
        break;
      }
      case TeamTaskAction::Delete: {
        // 依赖方存在 (非删除任务引用本任务) 时拒绝删除。
        for (const TeamTaskSnapshot& other : state.tasks) {
          if (other.id == existing->id) continue;
          if (other.status == TeamTaskStatus::Deleted) continue;
          for (const std::string& blocker : other.blockedBy) {
            if (blocker == existing->id) {
              throw TeamError("team task \"" + existing->id
                                  + "\" is a dependency of \"" + other.id + "\"",
                              TEAM_TASK_HAS_DEPENDENTS);
            }
          }
        }
        updated.status = TeamTaskStatus::Deleted;
        break;
      }
    }

    journal.appendAndFlush(
        TeamTaskEventData{TEAM_EVENT_VERSION, lead.id(), updated});
  });
  return updated;
}

TeamTaskSnapshot TeamTaskBoard::get(const TeamJournal& journal,
                                    const std::string& taskId) const {
  TeamTaskSnapshot snapshot;
  journal.readState([&](const TeamFoldState& state) {
    const TeamTaskSnapshot* task = state.taskById(taskId);
    if (task == nullptr) {
      throw TeamError("team task \"" + taskId + "\" does not exist",
                      TEAM_TASK_NOT_FOUND);
    }
    if (task->status == TeamTaskStatus::Deleted) {
      throw TeamError("team task \"" + taskId + "\" is deleted",
                      TEAM_TASK_DELETED);
    }
    snapshot = *task;
  });
  return snapshot;
}

std::vector<TeamTaskView> TeamTaskBoard::list(const TeamJournal& journal) const {
  std::vector<TeamTaskView> views;
  journal.readState([&](const TeamFoldState& state) {
    for (const TeamTaskSnapshot& task : state.tasks) {
      if (task.status == TeamTaskStatus::Deleted) continue;
      TeamTaskView view;
      view.id = task.id;
      view.revision = task.revision;
      view.subject = task.subject;
      view.description = task.description;
      view.status = task.status;
      view.blockedBy = task.blockedBy;
      view.writeScopes = task.writeScopes;
      if (task.ownerId.has_value()) {
        if (const TeamMemberSnapshot* owner = state.memberById(*task.ownerId)) {
          view.ownerName = owner->name;
        } else if (*task.ownerId == lead.id()) {
          view.ownerName = "lead";
        }
      }
      view.ready = task.status == TeamTaskStatus::Pending;
      for (const std::string& blocker : task.blockedBy) {
        const TeamTaskSnapshot* ref = state.taskById(blocker);
        if (ref == nullptr || ref->status == TeamTaskStatus::Deleted) continue;
        if (ref->status != TeamTaskStatus::Completed) view.ready = false;
      }
      for (const std::string& scope : task.writeScopes) {
        if (state.memberByName(scope) == nullptr) {
          view.writeScopeWarnings.push_back(scope);
        }
      }
      views.push_back(std::move(view));
    }
  });
  return views;
}

}
