#include "TeamMailbox.hpp"

#include <stdexcept>
#include <utility>

#include "avox/module/LogHelper.hpp"
#include "avox_agent/core/Session.hpp"

namespace avox {

namespace {

// 目标解析结果: Lead 或某个成员快照 (事务内的折叠状态引用, 用完即弃)。
struct ResolvedTarget {
  bool isLead = false;
  const TeamMemberSnapshot* member = nullptr;
};

// dsh 目标解析: 'lead' | Lead id | 队友名 | 队友 id; 其余 TEAM_INVALID_TARGET。
ResolvedTarget resolveTarget(const TeamFoldState& state, const Agent& lead,
                             const std::string& target) {
  ResolvedTarget resolved;
  if (target == "lead" || target == lead.id().value) {
    resolved.isLead = true;
    return resolved;
  }
  resolved.member = state.memberByName(target);
  if (resolved.member == nullptr) {
    // 名字优先; 再按成员 id 试一次。
    for (const TeamMemberSnapshot& member : state.members) {
      if (member.id.value == target) {
        resolved.member = &member;
        break;
      }
    }
  }
  if (resolved.member == nullptr) {
    throw TeamError("team target \"" + target + "\" does not exist",
                    TEAM_INVALID_TARGET);
  }
  return resolved;
}

// 正文文本: 文本块按行拼接; 非文本块不进帧 (邮箱消息是文本通道, 快照仍保完整块)。
std::string textOf(const std::vector<ContentBlock>& content) {
  std::string text;
  for (const ContentBlock& block : content) {
    if (const auto* textBlock = std::get_if<TextBlock>(&block)) {
      if (!text.empty()) text += "\n";
      text += textBlock->text;
    }
  }
  return text;
}

// dsh deliveryContent 框头。
std::string frameOf(const TeamMessageSnapshot& message) {
  return "Team message " + message.id + " from " + message.senderName + ":\n"
         + textOf(message.content);
}

// 投递帧消息: id = 邮箱消息 id (目标 inbox 的唯一性校验靠它), source 带 team 标记。
UserMessage deliveryMessage(const SessionId& teamId,
                            const TeamMessageSnapshot& message) {
  UserMessage delivered;
  delivered.id = MessageId(message.id);
  delivered.content = {ContentBlock{TextBlock{frameOf(message)}}};
  delivered.source = teamMessageSource(teamId.value, message.id,
                                       message.senderId.value,
                                       message.senderName);
  return delivered;
}

// 投递确认: send() 同步落 inbox splice —— 返回后扫目标日志, 找到带本 messageId 的
// user/message 或 inbox/spliced 即确认。倒扫 (最近的必然在尾部附近)。
bool deliveredTo(Agent& target, const std::string& messageId) {
  bool found = false;
  target.withSession([&](const Session& session) {
    const std::vector<SessionEvent>& events = session.events();
    for (auto it = events.rbegin(); it != events.rend(); ++it) {
      if (found) break;
      if (it->type == EventType::InboxSpliced) {
        const InboxSplicedData& data = std::get<InboxSplicedData>(it->data);
        for (const UserMessage& inserted : data.inserted) {
          if (inserted.source.kind == MessageSourceKind::TeamMessage
              && inserted.source.messageId.value_or("") == messageId) {
            found = true;
            break;
          }
        }
      } else if (it->type == EventType::UserMessageEvent) {
        const UserMessageData& data = std::get<UserMessageData>(it->data);
        if (data.message.source.kind == MessageSourceKind::TeamMessage
            && data.message.source.messageId.value_or("") == messageId) {
          found = true;
        }
      }
    }
  });
  return found;
}

}  // namespace

TeamMailbox::TeamMailbox(Agent& leadAgent, TeamConfig configValue)
    : lead(leadAgent), config(configValue) {}

size_t TeamMailbox::pendingCountFor(const TeamFoldState& state,
                                    const SessionId& targetId) {
  size_t pending = 0;
  for (const TeamMessageSnapshot& message : state.queued) {
    if (!(message.targetId == targetId)) continue;
    if (state.delivered.count(message.id) == 0) ++pending;
  }
  return pending;
}

SendTeamMessageResult TeamMailbox::send(
    TeamJournal& journal, TeamRoster& roster, const Agent& sender,
    const std::string& senderName, const SendTeamMessageRequest& request) {
  SendTeamMessageResult result;
  // 快照文本先行: 大小校验在入队前 (入队即持久, 超限消息必须拒之门外)。
  std::string frame;
  journal.transact([&]() {
    TeamFoldState& state = journal.state();
    const ResolvedTarget target = resolveTarget(state, lead, request.target);
    if (target.isLead) {
      if (&sender == &lead) {
        throw TeamError("lead cannot send a team message to itself",
                        TEAM_SELF_MESSAGE);
      }
    } else if (target.member->id == sender.id()) {
      throw TeamError("team members cannot send messages to themselves",
                      TEAM_SELF_MESSAGE);
    } else if (target.member->phase == TeamMemberPhase::Provisioning) {
      throw TeamError("member \"" + target.member->name
                          + "\" is still provisioning",
                      TEAM_PROVISIONING_CONFLICT);
    } else if (target.member->phase != TeamMemberPhase::Active) {
      throw TeamError("member \"" + target.member->name + "\" has failed",
                      TEAM_MEMBER_NOT_FOUND);
    }

    TeamMessageSnapshot message;
    message.id = "msg-" + std::to_string(state.nextMessageNumber);
    message.senderId = sender.id();
    message.senderName = senderName;
    message.targetId =
        target.isLead ? lead.id() : target.member->id;
    message.delivery = request.delivery;
    message.content = request.content;
    frame = frameOf(message);

    if (frame.size() > static_cast<size_t>(config.maxMessageBytes)) {
      throw TeamError("team message frame is " + std::to_string(frame.size())
                          + " bytes (limit "
                          + std::to_string(config.maxMessageBytes) + ")",
                      TEAM_MESSAGE_TOO_LARGE);
    }
    if (pendingCountFor(state, message.targetId)
        >= static_cast<size_t>(config.maxPendingMessagesPerMember)) {
      throw TeamError("mailbox for \"" + request.target + "\" is full",
                      TEAM_MAILBOX_FULL);
    }

    result.messageId = message.id;
    journal.appendAndFlush(TeamMessageQueuedData{
        TEAM_EVENT_VERSION, lead.id(), std::move(message)});

    // ---- 尽力投递 (仍持 journalMtx: 锁序 journalMtx -> roster/目标 agent) ----
    if (target.isLead) {
      const TeamMessageSnapshot& queued = state.queued.back();
      result.accepted = deliverOnce(journal, lead, queued);
      return;
    }
    ReactLoopAgent* agent = roster.findLive(target.member->id);
    if (agent == nullptr) {
      if (request.delivery == TeamMessageDelivery::Wakeup) {
        // wakeup 到 Inactive 成员: 冷恢复后投递。恢复失败不回滚入队 —— 消息仍在
        // 持久邮箱里, 下次恢复再补投; 发送方看到 accepted=false。
        try {
          agent = &roster.ensureLive(*target.member);
        } catch (const std::exception& e) {
          LOGFLF(LogLevel::warn, "[team] wakeup 目标冷恢复失败 (消息已排队): ",
                 e.what());
          return;
        }
      } else {
        // quiet 到 Inactive 成员: 停在邮箱, 等成员被唤醒后补投 (dsh 语义)。
        return;
      }
    }
    const TeamMessageSnapshot& queued = state.queued.back();
    result.accepted = deliverOnce(journal, *agent, queued);
  });
  return result;
}

bool TeamMailbox::deliverOnce(TeamJournal& journal, Agent& target,
                              const TeamMessageSnapshot& message) {
  // quiet -> inject (NextStep, 不唤醒); wakeup -> followup (NextTurn, 唤醒)。
  // send() 同步落 inbox splice, 返回即可校验。
  target.send(deliveryMessage(lead.id(), message),
              message.delivery == TeamMessageDelivery::Wakeup
                  ? InboxTarget::NextTurn
                  : InboxTarget::NextStep,
              message.delivery == TeamMessageDelivery::Wakeup);
  if (!deliveredTo(target, message.id)) {
    LOGFLF(LogLevel::warn, "[team] 消息 ", message.id.c_str(),
           " 投递后未见确认 (保持排队)");
    return false;
  }
  journal.appendAndFlush(TeamMessageDeliveredData{
      TEAM_EVENT_VERSION, lead.id(), message.id, target.id()});
  return true;
}

void TeamMailbox::dispatchPending(TeamJournal& journal, TeamRoster& roster) {
  // 快照待投列表: 追加 delivered 会改 folded.queued/delivered, 边遍历边改不稳。
  std::vector<TeamMessageSnapshot> pending;
  {
    const TeamFoldState& state = journal.state();
    for (const TeamMessageSnapshot& message : state.queued) {
      if (state.delivered.count(message.id) == 0) pending.push_back(message);
    }
  }
  for (const TeamMessageSnapshot& message : pending) {
    if (journal.state().delivered.count(message.id) != 0) continue;
    Agent* target = nullptr;
    if (message.targetId == lead.id()) {
      target = &lead;
    } else {
      const TeamMemberSnapshot* member =
          journal.state().memberById(message.targetId);
      if (member == nullptr
          || member->phase != TeamMemberPhase::Active) {
        continue;  // 目标已非 active (failed) —— 邮箱保留, 不投递。
      }
      ReactLoopAgent* agent = roster.findLive(member->id);
      if (agent == nullptr) {
        if (message.delivery != TeamMessageDelivery::Wakeup) continue;
        try {
          agent = &roster.ensureLive(*member);
        } catch (const std::exception& e) {
          LOGFLF(LogLevel::warn, "[team] 恢复补投: 目标冷恢复失败: ",
                 e.what());
          continue;
        }
      }
      target = agent;
    }
    if (!deliverOnce(journal, *target, message)) continue;
    // 目标刚被唤醒 (冷恢复) 时, 它的邮箱里可能还有 quiet 积压 —— 本循环继续
    // 会把同目标的后续消息一并补投 (ensureLive 幂等)。
  }
}

}
