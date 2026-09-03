#include "TeamFold.hpp"

#include <stdexcept>
#include <utility>

namespace avox {

const TeamMemberSnapshot* TeamFoldState::memberById(const SessionId& id) const {
  for (const TeamMemberSnapshot& member : members) {
    if (member.id == id) return &member;
  }
  return nullptr;
}

TeamMemberSnapshot* TeamFoldState::memberById(const SessionId& id) {
  for (TeamMemberSnapshot& member : members) {
    if (member.id == id) return &member;
  }
  return nullptr;
}

const TeamMemberSnapshot* TeamFoldState::memberByName(
    const std::string& name) const {
  for (const TeamMemberSnapshot& member : members) {
    if (member.name == name) return &member;
  }
  return nullptr;
}

const TeamTaskSnapshot* TeamFoldState::taskById(const std::string& id) const {
  for (const TeamTaskSnapshot& task : tasks) {
    if (task.id == id) return &task;
  }
  return nullptr;
}

TeamTaskSnapshot* TeamFoldState::taskById(const std::string& id) {
  for (TeamTaskSnapshot& task : tasks) {
    if (task.id == id) return &task;
  }
  return nullptr;
}

namespace {

[[noreturn]] void corrupt(const std::string& message) {
  throw std::runtime_error("Lead 会话日志的 team 状态损坏: " + message);
}

// <prefix>-<n> 的数字后缀解析; 前缀不符或非纯数字返回 false。
bool numberOf(const std::string& id, const std::string& prefix,
              int64_t& number) {
  if (id.size() <= prefix.size()
      || id.compare(0, prefix.size(), prefix) != 0) {
    return false;
  }
  int64_t value = 0;
  for (size_t i = prefix.size(); i < id.size(); ++i) {
    const char c = id[i];
    if (c < '0' || c > '9') return false;
    value = value * 10 + (c - '0');
    if (value > 1000000000) return false;
  }
  number = value;
  return true;
}

void foldMember(TeamFoldState& state, const TeamMemberEventData& data) {
  const TeamMemberSnapshot& incoming = data.member;
  if (TeamMemberSnapshot* existing = state.memberById(incoming.id)) {
    // 不可变字段必须逐字一致。
    if (existing->name != incoming.name) {
      corrupt("member \"" + incoming.id.value
              + "\" changed name (immutable): \"" + existing->name
              + "\" -> \"" + incoming.name + "\"");
    }
    if (existing->provider != incoming.provider
        || existing->context != incoming.context) {
      corrupt("member \"" + incoming.name
              + "\" changed provider/context (immutable)");
    }
    // 生命周期: provisioning -> active | failed 各至多一次; active/failed 是终态。
    if (existing->phase != TeamMemberPhase::Provisioning) {
      corrupt("member \"" + incoming.name + "\" left terminal phase "
              + (existing->phase == TeamMemberPhase::Active ? "active"
                                                            : "failed"));
    }
    if (incoming.phase == TeamMemberPhase::Provisioning) {
      corrupt("member \"" + incoming.name + "\" re-entered provisioning");
    }
    *existing = incoming;
    return;
  }
  // 新成员: 名字永不复用 (含 failed 成员的墓碑名)。
  if (state.memberByName(incoming.name) != nullptr) {
    corrupt("member name \"" + incoming.name + "\" reused");
  }
  if (incoming.phase != TeamMemberPhase::Provisioning) {
    corrupt("member \"" + incoming.name + "\" must begin in provisioning");
  }
  state.members.push_back(incoming);
}

void foldTask(TeamFoldState& state, const TeamTaskSnapshot& incoming) {
  int64_t number = 0;
  if (!numberOf(incoming.id, "task-", number)) {
    corrupt("task id \"" + incoming.id + "\" is not task-<number>");
  }
  if (number >= state.nextTaskNumber) state.nextTaskNumber = number + 1;
  if (TeamTaskSnapshot* existing = state.taskById(incoming.id)) {
    if (incoming.revision != existing->revision + 1) {
      corrupt("task \"" + incoming.id + "\" revision "
              + std::to_string(incoming.revision) + " is not contiguous after "
              + std::to_string(existing->revision));
    }
    *existing = incoming;
    return;
  }
  if (incoming.revision != 1) {
    corrupt("task \"" + incoming.id + "\" must begin at revision 1");
  }
  state.tasks.push_back(incoming);
}

void foldQueued(TeamFoldState& state, const TeamMessageSnapshot& incoming) {
  int64_t number = 0;
  if (!numberOf(incoming.id, "msg-", number)) {
    corrupt("team message id \"" + incoming.id + "\" is not msg-<number>");
  }
  if (number >= state.nextMessageNumber) state.nextMessageNumber = number + 1;
  for (const TeamMessageSnapshot& message : state.queued) {
    if (message.id == incoming.id) {
      corrupt("team message \"" + incoming.id + "\" queued twice");
    }
  }
  state.queued.push_back(incoming);
}

void foldDelivered(TeamFoldState& state, const std::string& messageId,
                   const SessionId& targetId) {
  if (state.delivered.count(messageId) != 0) {
    corrupt("team message \"" + messageId + "\" delivered twice");
  }
  const TeamMessageSnapshot* message = nullptr;
  for (const TeamMessageSnapshot& queued : state.queued) {
    if (queued.id == messageId) {
      message = &queued;
      break;
    }
  }
  if (message == nullptr) {
    corrupt("team message \"" + messageId + "\" delivered before queued");
  }
  if (!(message->targetId == targetId)) {
    corrupt("team message \"" + messageId + "\" delivered to wrong target");
  }
  state.delivered.insert(messageId);
}

}  // namespace

void foldTeamEvent(TeamFoldState& state, const SessionEvent& event) {
  switch (event.type) {
    case EventType::TeamMember: {
      const auto& data = std::get<TeamMemberEventData>(event.data);
      // fork 前缀里继承的别队状态整条忽略。
      if (!(data.teamId == state.rootId)) return;
      foldMember(state, data);
      break;
    }
    case EventType::TeamTask: {
      const auto& data = std::get<TeamTaskEventData>(event.data);
      if (!(data.teamId == state.rootId)) return;
      foldTask(state, data.task);
      break;
    }
    case EventType::TeamMessageQueued: {
      const auto& data = std::get<TeamMessageQueuedData>(event.data);
      if (!(data.teamId == state.rootId)) return;
      foldQueued(state, data.message);
      break;
    }
    case EventType::TeamMessageDelivered: {
      const auto& data = std::get<TeamMessageDeliveredData>(event.data);
      if (!(data.teamId == state.rootId)) return;
      foldDelivered(state, data.messageId, data.targetId);
      break;
    }
    default:
      break;
  }
}

TeamFoldState foldTeam(const SessionId& rootId,
                       const std::vector<SessionEvent>& events) {
  TeamFoldState state;
  state.rootId = rootId;
  for (const SessionEvent& event : events) {
    foldTeamEvent(state, event);
  }
  return state;
}

}
