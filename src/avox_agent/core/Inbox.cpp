#include "Inbox.hpp"

#include <algorithm>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace avox {

Inbox::Inbox(Session& session, InboxNotifications notifications)
    : session(session), notifications(std::move(notifications)) {
  // 从日志折叠出当前队列。这些消息不是刚到的, 所以不发通知。
  for (const SessionEvent& event : session.events()) {
    if (event.type != EventType::InboxSpliced) continue;
    apply(std::get<InboxSplicedData>(event.data));
  }
}

std::vector<UserMessage>& Inbox::queueOf(InboxTarget target) {
  return target == InboxTarget::NextTurn ? nextTurnQueue : nextStepQueue;
}

const std::vector<UserMessage>& Inbox::queueOf(InboxTarget target) const {
  return target == InboxTarget::NextTurn ? nextTurnQueue : nextStepQueue;
}

bool Inbox::locate(const MessageId& id, Location& out) const {
  for (size_t i = 0; i < nextTurnQueue.size(); ++i) {
    if (nextTurnQueue[i].id == id) {
      out = Location{InboxTarget::NextTurn, i};
      return true;
    }
  }
  for (size_t i = 0; i < nextStepQueue.size(); ++i) {
    if (nextStepQueue[i].id == id) {
      out = Location{InboxTarget::NextStep, i};
      return true;
    }
  }
  return false;
}

std::vector<UserMessage> Inbox::claim(InboxTarget target, int turn) {
  // 永远先全量取 next-step。
  std::vector<UserMessage> claimed =
      mutate(InboxTarget::NextStep, 0, nextStepQueue.size(), {}, false);
  if (target == InboxTarget::NextTurn) {
    // 再取恰好一条 next-turn: 它是「每条各占一个 turn」的实现方式。
    std::vector<UserMessage> queued =
        mutate(InboxTarget::NextTurn, 0, 1, {}, false);
    for (UserMessage& message : queued) claimed.push_back(std::move(message));
  }
  if (notifications.claimed != nullptr) {
    for (const UserMessage& message : claimed) {
      notifications.claimed(message, turn);
    }
  }
  return claimed;
}

std::vector<UserMessage> Inbox::splice(InboxTarget target, size_t start,
                                       size_t deleteCount,
                                       std::vector<UserMessage> inserted) {
  return mutate(target, start, deleteCount, std::move(inserted), true);
}

void Inbox::append(InboxTarget target, UserMessage message) {
  std::vector<UserMessage> inserted;
  inserted.push_back(std::move(message));
  mutate(target, queueOf(target).size(), 0, std::move(inserted), true);
}

void Inbox::prepend(InboxTarget target, UserMessage message) {
  std::vector<UserMessage> inserted;
  inserted.push_back(std::move(message));
  mutate(target, 0, 0, std::move(inserted), true);
}

bool Inbox::remove(const MessageId& id) {
  Location location{};
  if (!locate(id, location)) return false;
  mutate(location.target, location.index, 1, {}, true);
  return true;
}

void Inbox::clear() {
  if (!nextStepQueue.empty()) {
    mutate(InboxTarget::NextStep, 0, nextStepQueue.size(), {}, true);
  }
  if (!nextTurnQueue.empty()) {
    mutate(InboxTarget::NextTurn, 0, nextTurnQueue.size(), {}, true);
  }
}

std::vector<UserMessage> Inbox::mutate(InboxTarget target, size_t start,
                                       size_t deleteCount,
                                       std::vector<UserMessage> inserted,
                                       bool discardRemoved) {
  std::vector<UserMessage>& queue = queueOf(target);

  // 坐标规范化: 落进日志的必须是已经算好的确定坐标, 而不是调用方的意图 ——
  // 否则回放方还得复现一遍截断规则才能重建同一个队列。
  const size_t actualStart = std::min(start, queue.size());
  const size_t actualDeleteCount =
      std::min(deleteCount, queue.size() - actualStart);
  if (actualDeleteCount == 0 && inserted.empty()) return {};

  InboxSplicedData splice;
  splice.target = target;
  splice.start = actualStart;
  if (actualDeleteCount != 0) splice.removedCount = actualDeleteCount;
  splice.inserted = std::move(inserted);
  // dsh 键名 outcome: 只有「被移除的消息算取消」才写 (claim 的纯删除不写)。
  if (discardRemoved && actualDeleteCount > 0) {
    splice.outcome = std::string("canceled");
  }

  validate(splice);

  // 持久事件先提交, 活投影后变更: 同步观察者看到的是变更前的队列, 并能从上面这组
  // 规范化坐标重建被移除的消息。
  session.append(splice);

  std::vector<UserMessage> removed;
  removed.reserve(actualDeleteCount);
  for (size_t i = 0; i < actualDeleteCount; ++i) {
    removed.push_back(std::move(queue[actualStart + i]));
  }
  const auto first = queue.begin() + static_cast<ptrdiff_t>(actualStart);
  queue.erase(first, first + static_cast<ptrdiff_t>(actualDeleteCount));
  queue.insert(queue.begin() + static_cast<ptrdiff_t>(actualStart),
               splice.inserted.begin(), splice.inserted.end());

  if (discardRemoved && notifications.discarded != nullptr) {
    for (const UserMessage& message : removed) notifications.discarded(message);
  }
  if (notifications.inserted != nullptr) {
    for (const UserMessage& message : splice.inserted) {
      notifications.inserted(message);
    }
  }
  return removed;
}

void Inbox::apply(const InboxSplicedData& splice) {
  validate(splice);
  std::vector<UserMessage>& queue = queueOf(splice.target);
  const size_t removedCount = splice.removedCount.value_or(0);
  const auto first = queue.begin() + static_cast<ptrdiff_t>(splice.start);
  queue.erase(first, first + static_cast<ptrdiff_t>(removedCount));
  queue.insert(queue.begin() + static_cast<ptrdiff_t>(splice.start),
               splice.inserted.begin(), splice.inserted.end());
}

void Inbox::validate(const InboxSplicedData& splice) const {
  const std::vector<UserMessage>& queue = queueOf(splice.target);
  const size_t removedCount = splice.removedCount.value_or(0);
  if (splice.start > queue.size()
      || splice.start + removedCount > queue.size()) {
    throw std::runtime_error("inbox splice 坐标越界");
  }

  // 消息身份在两条队列里全局唯一: 同一条消息不得同时在两处待处理, 否则它会被处理两次,
  // 而模型会看到重复的输入。
  std::unordered_set<std::string> ids;
  const auto addAll = [&](const std::vector<UserMessage>& messages) {
    for (const UserMessage& message : messages) {
      if (!ids.insert(message.id.value).second) {
        throw std::runtime_error("消息 \"" + message.id.value + "\" 已在待处理队列中");
      }
    }
  };

  // 候选队列 = 应用本次 splice 之后的样子。
  std::vector<UserMessage> candidate;
  candidate.reserve(queue.size() - removedCount + splice.inserted.size());
  for (size_t i = 0; i < splice.start; ++i) candidate.push_back(queue[i]);
  for (const UserMessage& message : splice.inserted) candidate.push_back(message);
  for (size_t i = splice.start + removedCount; i < queue.size(); ++i) {
    candidate.push_back(queue[i]);
  }

  addAll(candidate);
  addAll(splice.target == InboxTarget::NextTurn ? nextStepQueue : nextTurnQueue);
}

}
