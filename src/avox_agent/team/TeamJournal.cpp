#include "TeamJournal.hpp"

#include <utility>

#include "avox/module/LogHelper.hpp"

namespace avox {

TeamJournal::TeamJournal(Agent& leadAgent, SessionWriter& leadWriter,
                         TeamActivity& teamActivity)
    : lead(leadAgent), writer(leadWriter), activity(teamActivity) {
  foldState.rootId = lead.id();
}

void TeamJournal::transact(const std::function<void()>& fn) {
  std::lock_guard<std::mutex> lock(journalMtx);
  fn();
}

TeamFoldState& TeamJournal::state() { return foldState; }

void TeamJournal::appendAndFlush(const EventData& data) {
  // append 走 Lead 的会话临界区: 与 Lead 驱动的边界事件串行, 观察者 (SessionWriter)
  // 在其中安全落行。观察者契约禁止回调进 agent 方法 —— 本层遵守, 折叠在锁内做纯计算。
  lead.withSession([&](Session& session) {
    const size_t seq = session.append(data);
    // 增量折叠: 事件已提交, 状态必须同步前进 (append 与 fold 之间没有别的事务能插入
    // —— journalMtx 由 transact 持有)。
    SessionEvent event;
    event.type = eventTypeOf(data);
    event.seq = seq;
    event.data = data;
    foldTeamEvent(foldState, event);
  });
  writer.flush();
  if (!writer.lastError().empty()) {
    LOGFLF(LogLevel::warn, "[team] Lead 日志刷盘失败: ",
           writer.lastError().c_str());
  }
  activity.notifyChanged();
}

void TeamJournal::reloadFromLead() {
  lead.withSession([this](Session& session) {
    TeamFoldState fresh = foldTeam(lead.id(), session.events());
    foldState = std::move(fresh);
  });
}

void TeamJournal::readState(
    const std::function<void(const TeamFoldState&)>& fn) const {
  std::lock_guard<std::mutex> lock(journalMtx);
  fn(foldState);
}

}
