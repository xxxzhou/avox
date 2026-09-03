#include "TeamRoster.hpp"

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <stdexcept>
#include <utility>

#include "avox/module/LogHelper.hpp"
#include "avox_agent/core/DshLayout.hpp"
#include "avox_agent/core/Llm.hpp"
#include "avox_agent/core/Session.hpp"
#include "TeamValidation.hpp"

namespace avox {

namespace {

// 建立子会话的 provider 名 (与 Subagents 的进程内 spawn provider 同键)。
const char* kSpawnProviderName = "spawn";

// checkpoint 等待的硬上限: user/message 落在首次模型请求之前, 正常毫秒级;
// 上限只防驱动失灵把 spawn 卡成永久挂起。
constexpr int64_t kCheckpointTimeoutMs = 120000;
// 等待的分片长度 —— 每片复检父取消信号。
constexpr int64_t kWaitSliceMs = 50;

// 子会话 id: 与宿主同命名法 "session-<秒>"; 同秒占用加 "-<n>"。zstd 档也算占用。
// (Subagents.cpp 的同款逻辑在匿名命名空间, 无法共用 —— 语义必须保持一字不差。)
std::string allocateChildSessionId(const std::string& sessionRoot,
                                   const std::optional<std::string>& cwd,
                                   const SessionId& parentId) {
  const int64_t seconds =
      std::chrono::duration_cast<std::chrono::seconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count();
  const std::string base = "session-" + std::to_string(seconds);
  for (int attempt = 1;; ++attempt) {
    const std::string candidate =
        attempt == 1 ? base : base + "-" + std::to_string(attempt);
    if (candidate == parentId.value) continue;
    const std::string path =
        dshSessionLogPath(sessionRoot, cwd, SessionId(candidate));
    if (!std::filesystem::exists(path)
        && !std::filesystem::exists(path + ".zstd")) {
      return candidate;
    }
  }
}

// fork 种子: Lead 日志截到最后一条 turn/end (含) —— 半开的 turn 不携带。
std::vector<SessionEvent> forkSeedOf(Agent& lead) {
  std::vector<SessionEvent> events;
  lead.withSession([&](Session& session) { events = session.events(); });
  for (size_t i = events.size(); i > 0; --i) {
    if (events[i - 1].type == EventType::TurnEnd) {
      events.resize(i);
      return events;
    }
  }
  events.clear();
  return events;
}

// checkpointInitialPrompt 的观察者: 等子日志出现首条 source=user 的 user/message,
// 或 turn 先收尾 (= 初始提示被拒, 不会发生模型调用)。
//
// 回调运行在子 agent 锁内 —— 只碰自己的小锁 (叶子), 不回调任何 agent 方法。
class InitialPromptWatch final : public SessionObserver {
 public:
  explicit InitialPromptWatch(MessageId expected) : expected(std::move(expected)) {}

  void onSessionEvent(const Session& session,
                      const SessionEvent& event) override {
    std::lock_guard<std::mutex> lock(mtx);
    if (!recorded && !turnEnded) {
      if (event.type == EventType::UserMessageEvent) {
        const UserMessageData& data = std::get<UserMessageData>(event.data);
        if (data.message.source.kind == MessageSourceKind::User
            && data.message.id == expected) {
          recorded = true;
        }
      } else if (event.type == EventType::TurnEnd) {
        turnEnded = true;
      }
    }
    if (recorded || turnEnded) cv.notify_all();
  }

  // 等到 recorded 或 turnEnded。signal 触发时对 agent 补一刀 cancel (交接缝关门,
  // 与 Subagents 的复检同理: cancel 对空闲 agent 是空操作), 之后继续等到收尾。
  // 返回是否 recorded。
  bool waitOutcome(int64_t timeoutMs, const std::shared_ptr<AbortSignal>& signal,
                   Agent& agent) {
    std::unique_lock<std::mutex> lock(mtx);
    const auto deadline = std::chrono::steady_clock::now()
                        + std::chrono::milliseconds(timeoutMs);
    bool cascaded = false;
    while (!recorded && !turnEnded) {
      if (signal != nullptr && signal->aborted() && !cascaded) {
        cascaded = true;
        lock.unlock();
        agent.cancel(AgentCancelCause{CancelByParent{}});
        lock.lock();
        continue;
      }
      if (cv.wait_until(lock, deadline) == std::cv_status::timeout) break;
    }
    return recorded;
  }

 private:
  MessageId expected;
  std::mutex mtx;
  std::condition_variable cv;
  bool recorded = false;
  bool turnEnded = false;
};

// 子日志里的 continuable 描述符 (首条); 无则 nullopt。
const SubagentDescriptorData* findDescriptor(
    const std::vector<SessionEvent>& events) {
  for (const SessionEvent& event : events) {
    if (event.type != EventType::SubagentDescriptor) continue;
    return &std::get<SubagentDescriptorData>(event.data);
  }
  return nullptr;
}

bool hasRecordedInitialPrompt(const std::vector<SessionEvent>& events) {
  for (const SessionEvent& event : events) {
    if (event.type != EventType::UserMessageEvent) continue;
    const UserMessageData& data = std::get<UserMessageData>(event.data);
    if (data.message.source.kind == MessageSourceKind::User) return true;
  }
  return false;
}

}  // namespace

TeamRoster::TeamRoster(TeamConfig configValue, TeamRosterDeps depsValue)
    : config(configValue), deps(std::move(depsValue)) {}

// ---- 冷校验 ----

TeamChildLogCheck TeamRoster::checkChildLog(const std::string& path,
                                            const SessionId& leadId,
                                            const TeamMemberSnapshot& member) {
  TeamChildLogCheck check;
  if (path.empty() || !std::filesystem::exists(path)) {
    check.error = "child session log is missing";
    return check;
  }
  LoadedSession loaded;
  try {
    loaded = loadSession(path);
  } catch (const std::exception& e) {
    check.error = std::string("child session log unreadable: ") + e.what();
    return check;
  }
  if (!loaded.header.parentSession.has_value()
      || !(*loaded.header.parentSession == leadId)) {
    check.error = "child session log is not parented by this team lead";
    return check;
  }
  if (loaded.header.origin.value_or("") != "subagent") {
    check.error = "child session log origin is not subagent";
    return check;
  }
  const SubagentDescriptorData* descriptor = findDescriptor(loaded.events);
  if (descriptor == nullptr) {
    check.error = "child session log has no subagent descriptor";
    return check;
  }
  if (descriptor->mode != SubagentMode::Continuable) {
    check.error = "child session log descriptor is not continuable";
    return check;
  }
  if (descriptor->agentProvider.has_value()
      && *descriptor->agentProvider != member.provider) {
    check.error = "child session descriptor provider \""
                  + *descriptor->agentProvider
                  + "\" does not match member provider \"" + member.provider + "\"";
    return check;
  }
  if (!hasRecordedInitialPrompt(loaded.events)) {
    check.error = "child session log has no recorded initial prompt";
    return check;
  }
  check.ok = true;
  return check;
}

// ---- 存活管理 ----

ReactLoopAgent* TeamRoster::findLive(const SessionId& memberId) const {
  std::lock_guard<std::mutex> lock(rosterMtx);
  const auto it = live.find(memberId.value);
  return it == live.end() ? nullptr : it->second.agent.get();
}

std::unique_ptr<TeamLiveMember> TeamRoster::resumeFromDisk(
    const TeamMemberSnapshot& member) {
  const std::string path =
      dshSessionLogPath(deps.sessionRoot, deps.sessionCwd, member.id);
  const TeamChildLogCheck check =
      checkChildLog(path, deps.lead->id(), member);
  if (!check.ok) {
    throw std::runtime_error("cannot resume member \"" + member.name
                             + "\": " + check.error);
  }
  LoadedSession loaded = loadSession(path);

  // 先取 id 再搬 header: 同一调用里 loaded.header.id 与 std::move(loaded.header)
  // 的求值顺序未指定, 右到左求值会读到被搬空的 id。
  const SessionId resumedId = loaded.header.id;
  auto session = std::make_unique<Session>(resumedId, std::move(loaded.events),
                                           std::move(loaded.header));
  const SubagentDescriptorData* descriptor = findDescriptor(session->events());
  AgentOptions options;
  options.provider = member.provider;
  if (descriptor != nullptr && descriptor->agentModel.has_value()) {
    options.model = *descriptor->agentModel;
  }
  options.maxTokens = deps.leadOptions.maxTokens;
  options.subagentDepth = session->getHeader().delegationDepth.value_or(
      deps.memberDelegationDepth);

  ReactLoopAgent::Deps agentDeps;
  agentDeps.systemPrompt = deps.systemPrompt;
  agentDeps.tools = deps.tools;
  agentDeps.llm = deps.llm;
  agentDeps.points = deps.points;
  agentDeps.maxParallelToolCalls = deps.maxParallelToolCalls;

  auto entry = std::make_unique<TeamLiveMember>();
  entry->agent = std::make_unique<ReactLoopAgent>(std::move(session), options,
                                                  agentDeps);
  std::filesystem::create_directories(
      std::filesystem::path(path).parent_path());
  entry->writer = std::make_unique<SessionWriter>();
  if (!entry->writer->attach(entry->agent->session(), path)) {
    LOGFLF(LogLevel::warn, "[team] 队友日志无法落盘: ",
           entry->writer->lastError().c_str());
  }
  AgentLifecyclePayload createdPayload;
  createdPayload.agent = entry->agent.get();
  deps.points->created.emit(createdPayload, entry->agent->scope());
  LOGFLF(LogLevel::info, "[team] 队友 ", member.name.c_str(), " (",
         member.id.value.c_str(), ") 冷恢复");
  return entry;
}

ReactLoopAgent& TeamRoster::ensureLive(const TeamMemberSnapshot& member) {
  {
    std::lock_guard<std::mutex> lock(rosterMtx);
    const auto it = live.find(member.id.value);
    if (it != live.end()) return *it->second.agent;
  }
  // 冷恢复在 rosterMtx 外做 (读盘 + 建驱动); 并发恢复同一成员时后到者拆掉自己那份。
  std::unique_ptr<TeamLiveMember> entry = resumeFromDisk(member);
  ReactLoopAgent* resolved = entry->agent.get();
  {
    std::lock_guard<std::mutex> lock(rosterMtx);
    const auto it = live.find(member.id.value);
    if (it != live.end()) {
      // 别人先到: 拆掉这份重复的 (它从未消费输入, 直接 shutdown 安全)。
      entry->agent->shutdown();
      AgentLifecyclePayload disposedPayload;
      disposedPayload.agent = entry->agent.get();
      deps.points->disposed.emit(disposedPayload, entry->agent->scope());
      entry->writer->detach();
      return *it->second.agent;
    }
    live.emplace(member.id.value, std::move(*entry));
  }
  return *resolved;
}

InterruptResult TeamRoster::interrupt(const TeamMemberSnapshot& member) {
  ReactLoopAgent* agent = findLive(member.id);
  if (agent == nullptr) {
    throw TeamError("member \"" + member.name + "\" has no live runtime to interrupt",
                    TEAM_INVALID_TARGET);
  }
  InterruptResult result;
  result.previousStatus = agent->status() == AgentStatus::Running
                              ? TeamMemberRuntimeStatus::Running
                              : TeamMemberRuntimeStatus::Idle;
  // keepInbox=true: 已排队的工作保留 (dsh interrupt 不清邮箱)。
  agent->cancel(AgentCancelCause{CancelByUser{}}, true);
  return result;
}

TeamMemberRuntimeStatus TeamRoster::statusOf(
    const TeamMemberSnapshot& member) const {
  if (const ReactLoopAgent* agent = findLive(member.id)) {
    return agent->status() == AgentStatus::Running
               ? TeamMemberRuntimeStatus::Running
               : TeamMemberRuntimeStatus::Idle;
  }
  switch (member.phase) {
    case TeamMemberPhase::Active:
      return TeamMemberRuntimeStatus::Inactive;
    case TeamMemberPhase::Provisioning:
      return TeamMemberRuntimeStatus::Provisioning;
    case TeamMemberPhase::Failed:
      return TeamMemberRuntimeStatus::Failed;
  }
  return TeamMemberRuntimeStatus::Inactive;
}

// ---- spawn ----

TeamMemberSnapshot TeamRoster::spawn(TeamJournal& journal,
                                     const SpawnTeammateRequest& request) {
  if (deps.sessionRoot.empty()) {
    throw TeamError("sessionRoot must be configured to start teammates",
                    TEAM_INVALID_ARGUMENT);
  }
  // ---- 段 1: 事务校验 + provisioning ----
  TeamMemberSnapshot snapshot;
  journal.transact([&]() {
    TeamFoldState& state = journal.state();
    checkTeamMemberName(request.name);
    if (state.memberByName(request.name) != nullptr) {
      throw TeamError("member name \"" + request.name + "\" is already taken",
                      TEAM_MEMBER_NAME_TAKEN);
    }
    if (static_cast<int>(state.members.size()) >= config.maxMembers) {
      throw TeamError("team member limit reached (" +
                          std::to_string(config.maxMembers) + ")",
                      TEAM_MEMBER_LIMIT);
    }
    snapshot.id = SessionId(allocateChildSessionId(
        deps.sessionRoot, deps.sessionCwd, deps.lead->id()));
    snapshot.name = request.name;
    snapshot.description = request.description;
    snapshot.provider = request.provider.empty() ? deps.leadOptions.provider
                                                 : request.provider;
    snapshot.context = request.context;
    snapshot.phase = TeamMemberPhase::Provisioning;
    journal.appendAndFlush(
        TeamMemberEventData{TEAM_EVENT_VERSION, deps.lead->id(), snapshot});
  });

  // ---- 段 2: 锁外起动 (子会话 + 初始提示 + checkpoint) ----
  std::string failure;
  std::unique_ptr<TeamLiveMember> entry;
  try {
    entry = startContinuable(snapshot, request);
    // 起动即入注册表 (settle 前): phase 仍为 provisioning, 邮箱尚不会投给它 ——
    // 注册表先就位, settle 成功的瞬间即可寻址。
    std::lock_guard<std::mutex> lock(rosterMtx);
    const auto it = live.find(snapshot.id.value);
    if (it != live.end()) {
      throw std::runtime_error("duplicate live runtime for member \""
                               + snapshot.name + "\"");
    }
    live.emplace(snapshot.id.value, std::move(*entry));
    entry.reset();
  } catch (const std::exception& e) {
    failure = e.what();
  }
  if (failure.empty() && request.signal != nullptr
      && request.signal->aborted()) {
    failure = "spawn aborted during provisioning";
  }
  if (!failure.empty() && entry != nullptr) {
    // 起动失败的活驱动现场拆除 (子日志保留 —— failed 成员的审计轨迹)。
    entry->agent->shutdown();
    AgentLifecyclePayload disposedPayload;
    disposedPayload.agent = entry->agent.get();
    deps.points->disposed.emit(disposedPayload, entry->agent->scope());
    entry->writer->detach();
    entry.reset();
  }

  // ---- 段 3: 事务 settle ----
  if (failure.empty()) {
    snapshot.phase = TeamMemberPhase::Active;
    snapshot.error.reset();
    try {
      journal.transact([&]() {
        journal.appendAndFlush(
            TeamMemberEventData{TEAM_EVENT_VERSION, deps.lead->id(), snapshot});
      });
    } catch (const std::exception& e) {
      // 活跃标记没落盘: 成员在跑但状态停在 provisioning —— 恢复期 reconcile 按
      // 子日志冷裁定回 active, 自愈。返回仍报 active (运行事实)。
      LOGFLF(LogLevel::warn, "[team] 成员 active 标记落盘失败 (恢复期自愈): ",
             e.what());
    }
    return snapshot;
  }
  snapshot.phase = TeamMemberPhase::Failed;
  snapshot.error = failure;
  journal.transact([&]() {
    journal.appendAndFlush(
        TeamMemberEventData{TEAM_EVENT_VERSION, deps.lead->id(), snapshot});
  });
  LOGFLF(LogLevel::warn, "[team] 队友 ", snapshot.name.c_str(), " 起动失败: ",
         failure.c_str());
  return snapshot;
}

std::unique_ptr<TeamLiveMember> TeamRoster::startContinuable(
    const TeamMemberSnapshot& snapshot, const SpawnTeammateRequest& request) {
  const std::string childPath =
      dshSessionLogPath(deps.sessionRoot, deps.sessionCwd, snapshot.id);

  // ---- 子会话: fresh 空日志; fork 继承 Lead 到最后一条 turn/end 的前缀 ----
  std::vector<SessionEvent> seed;
  if (request.context == TeamMemberContext::Fork) {
    seed = forkSeedOf(*deps.lead);
  }
  SessionHeader childHeader;
  childHeader.version = SESSION_FORMAT_VERSION;
  childHeader.id = snapshot.id;
  childHeader.createdAt =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count();
  childHeader.cwd = deps.sessionCwd;
  childHeader.parentSession = deps.lead->id();
  if (!seed.empty()) childHeader.seedLength = seed.size();
  childHeader.delegationDepth = deps.memberDelegationDepth;
  childHeader.origin = "subagent";
  auto session = std::make_unique<Session>(snapshot.id, std::move(seed),
                                           std::move(childHeader));
  // 审批钉 never —— 队友是自主后台工, 没有交互确认通道 (同 one-shot 委派)。
  session->append(
      ApprovalPolicyData{ApprovalPolicy::Never, std::string("delegation")});
  // 描述符在首条 turn 之前追加。dsh 的 provider 在首个 turn 内、首次请求前落;
  // avox 提前到 pre-turn —— 避免运行期向子作用域注册 pre-step 钩子 (ScopedLayers
  // 无内部锁, 起动线程插入会与其它队友驱动线程的装配读并发)。折叠方只认首条且
  // 位置不敏感, wire 字节完全一致。
  SubagentDescriptorData descriptor;
  descriptor.mode = SubagentMode::Continuable;
  descriptor.provider = kSpawnProviderName;
  descriptor.label = snapshot.name;
  descriptor.agentProvider = snapshot.provider;
  if (request.provider.empty() && !deps.leadOptions.model.empty()) {
    descriptor.agentModel = deps.leadOptions.model;
  }
  session->append(descriptor);

  // ---- 驱动: 与 Lead 同批设施, 独立会话/日志/驱动线程/作用域 ----
  ReactLoopAgent::Deps agentDeps;
  agentDeps.systemPrompt = deps.systemPrompt;
  agentDeps.tools = deps.tools;
  agentDeps.llm = deps.llm;
  agentDeps.points = deps.points;
  agentDeps.maxParallelToolCalls = deps.maxParallelToolCalls;
  AgentOptions options;
  options.provider = snapshot.provider;
  if (descriptor.agentModel.has_value()) options.model = *descriptor.agentModel;
  options.maxTokens = deps.leadOptions.maxTokens;
  options.subagentDepth = deps.memberDelegationDepth;

  auto entry = std::make_unique<TeamLiveMember>();
  entry->agent = std::make_unique<ReactLoopAgent>(std::move(session), options,
                                                  agentDeps);

  // ---- 发布: 落盘对齐 + created 通知 (镜像 AgentHost::openAgent 顺序) ----
  std::filesystem::create_directories(
      std::filesystem::path(childPath).parent_path());
  entry->writer = std::make_unique<SessionWriter>();
  if (!entry->writer->attach(entry->agent->session(), childPath)) {
    LOGFLF(LogLevel::warn, "[team] 队友日志无法落盘: ",
           entry->writer->lastError().c_str());
  }
  AgentLifecyclePayload createdPayload;
  createdPayload.agent = entry->agent.get();
  deps.points->created.emit(createdPayload, entry->agent->scope());

  // ---- 初始提示 + checkpointInitialPrompt ----
  const MessageId initialId(snapshot.id.value + "/initial");
  InitialPromptWatch watch(initialId);
  // 观察者的挂/摘都经子会话临界区: removeObserver 时首轮 turn 可能仍在跑 (recorded
  // 在首次模型请求前即触发), 与驱动的 append 并发改 observers 向量是竞态。
  entry->agent->withSession(
      [&watch](Session& session) { session.addObserver(&watch); });
  UserMessage task;
  task.id = initialId;
  task.content = request.prompt;
  task.source = userSource();
  entry->agent->followup(std::move(task));
  const bool recorded =
      watch.waitOutcome(kCheckpointTimeoutMs, request.signal, *entry->agent);
  entry->agent->withSession(
      [&watch](Session& session) { session.removeObserver(&watch); });
  if (!recorded) {
    throw std::runtime_error(
        "initial prompt was not recorded before the first turn closed");
  }
  LOGFLF(LogLevel::info, "[team] 队友 ", snapshot.name.c_str(), " (",
         snapshot.id.value.c_str(), ") 已起动");
  return entry;
}

// ---- 恢复期 ----

void TeamRoster::reconcileProvisioning(TeamJournal& journal) {
  // 收集后逐个追加: appendAndFlush 会推进折叠状态, 边遍历边改状态不稳。
  std::vector<TeamMemberSnapshot> pending;
  {
    const TeamFoldState& state = journal.state();
    for (const TeamMemberSnapshot& member : state.members) {
      if (member.phase == TeamMemberPhase::Provisioning) pending.push_back(member);
    }
  }
  for (TeamMemberSnapshot member : pending) {
    const std::string path =
        dshSessionLogPath(deps.sessionRoot, deps.sessionCwd, member.id);
    const TeamChildLogCheck check =
        checkChildLog(path, deps.lead->id(), member);
    if (check.ok) {
      member.phase = TeamMemberPhase::Active;
      member.error.reset();
    } else {
      member.phase = TeamMemberPhase::Failed;
      member.error = check.error;
    }
    journal.appendAndFlush(
        TeamMemberEventData{TEAM_EVENT_VERSION, deps.lead->id(), member});
  }
}

// ---- 销毁 ----

void TeamRoster::disposeMember(const SessionId& memberId, int64_t budgetMs) {
  TeamLiveMember entry;
  {
    std::lock_guard<std::mutex> lock(rosterMtx);
    const auto it = live.find(memberId.value);
    if (it == live.end()) return;
    entry = std::move(it->second);
    live.erase(it);
  }
  // 预算内优雅静止, 之后 shutdown 无条件 join (C++ 杀不掉线程, 预算是软的)。
  entry.agent->cancel(AgentCancelCause{CancelByDisposed{}});
  if (budgetMs > 0) entry.agent->whenIdle(static_cast<int>(budgetMs));
  entry.agent->shutdown();
  AgentLifecyclePayload disposedPayload;
  disposedPayload.agent = entry.agent.get();
  deps.points->disposed.emit(disposedPayload, entry.agent->scope());
  entry.writer->detach();
}

void TeamRoster::disposeAll(int64_t budgetMs) {
  std::vector<std::string> ids;
  {
    std::lock_guard<std::mutex> lock(rosterMtx);
    for (const auto& item : live) ids.push_back(item.first);
  }
  for (size_t i = 0; i < ids.size(); ++i) {
    // 预算在剩余成员间均摊 (每位至少 1ms)。
    const int64_t remaining = ids.size() - i;
    disposeMember(SessionId(ids[i]),
                  budgetMs > 0 ? budgetMs / static_cast<int64_t>(remaining) : 0);
  }
}

}
