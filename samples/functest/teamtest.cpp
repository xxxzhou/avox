// Agent Teams 持久运行时的自测。
//
// 覆盖 src/avox_agent/team 的关键契约:
//   1. 折叠不变式 (fold): 重放即状态, 违反不变式的日志响亮失败;
//   2. 入参校验 (validation): 队友名 / write-scope 规范化;
//   3. 任务板 CAS (task-board): revision 乐观并发 + 状态机 + 依赖图 + write-scope;
//   4. continuable spawn (roster): 三段式起动, 真 ReactLoopAgent 驱动 + 脚本 LLM;
//   5. 持久邮箱 (mailbox): 排队/投递确认/惰性冷恢复唤醒/限额;
//   6. 恢复语义 (service): 进程重启模拟 —— 重折叠 + provisioning 冷裁定 + 补投。
//
// 全离线: LlmProvider 用脚本假件, 不发网络请求。直接编译 core + team 的源文件参与
// (与 sessiontest 同因: avox 用静态 CRT, core/team 是 DLL 内部设施, 不跨边界导出)。

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "avox_agent/core/DshLayout.hpp"
#include "avox_agent/core/Llm.hpp"
#include "avox_agent/core/ReactLoopAgent.hpp"
#include "avox_agent/core/Session.hpp"
#include "avox_agent/core/SessionPersistence.hpp"
#include "avox_agent/team/TeamError.hpp"
#include "avox_agent/team/TeamFold.hpp"
#include "avox_agent/team/TeamService.hpp"
#include "avox_agent/team/TeamValidation.hpp"

using namespace avox;

namespace {

namespace fs = std::filesystem;

int g_failures = 0;

#define CHECK(cond)                                                         \
  do {                                                                      \
    if (!(cond)) {                                                          \
      std::cout << "FAIL line " << __LINE__ << ": " << #cond << std::endl;  \
      ++g_failures;                                                         \
    }                                                                       \
  } while (0)

#define CHECK_EQ(actual, expected)                                          \
  do {                                                                      \
    const auto& actualValue = (actual);                                     \
    const auto& expectedValue = (expected);                                 \
    if (!(actualValue == expectedValue)) {                                  \
      std::cout << "FAIL line " << __LINE__ << ": " << #actual << " 不符"   \
                << std::endl;                                               \
      ++g_failures;                                                         \
    }                                                                       \
  } while (0)

#define CHECK_THROWS(expr)                                                  \
  do {                                                                      \
    bool threw = false;                                                     \
    try {                                                                   \
      expr;                                                                 \
    } catch (const std::exception&) {                                       \
      threw = true;                                                         \
    }                                                                       \
    if (!threw) {                                                           \
      std::cout << "FAIL line " << __LINE__ << ": 期望抛异常: " << #expr    \
                << std::endl;                                               \
      ++g_failures;                                                         \
    }                                                                       \
  } while (0)

#define CHECK_THROWS_WITH(expr, needle)                                     \
  do {                                                                      \
    bool threw = false;                                                     \
    std::string what;                                                       \
    try {                                                                   \
      expr;                                                                 \
    } catch (const std::exception& e) {                                     \
      threw = true;                                                         \
      what = e.what();                                                      \
    }                                                                       \
    if (!threw || what.find(needle) == std::string::npos) {                 \
      std::cout << "FAIL line " << __LINE__ << ": 期望抛含 \"" << needle    \
                << "\" 的异常: " << #expr << " (实际 "                      \
                << (threw ? "\"" + what + "\"" : std::string("未抛")) << ")" \
                << std::endl;                                               \
      ++g_failures;                                                         \
    }                                                                       \
  } while (0)

// 抛出的 TeamError 须携指定稳定码 (dsh 错误词汇逐字对应, 模型依码自愈)。
#define CHECK_TEAM_ERROR(expr, expectedCode)                                \
  do {                                                                      \
    bool threw = false;                                                     \
    std::string code;                                                       \
    try {                                                                   \
      expr;                                                                 \
    } catch (const TeamError& e) {                                          \
      threw = true;                                                         \
      code = e.code;                                                        \
    } catch (const std::exception& e) {                                     \
      threw = true;                                                         \
      code = std::string("其它异常: ") + e.what();                          \
    }                                                                       \
    if (!threw || code != expectedCode) {                                   \
      std::cout << "FAIL line " << __LINE__ << ": 期望 TeamError "          \
                << expectedCode << " (实际 "                                \
                << (threw ? "\"" + code + "\"" : std::string("未抛")) << ")" \
                << std::endl;                                               \
      ++g_failures;                                                         \
    }                                                                       \
  } while (0)

// ---- 脚本 LLM: 每次调用吐一条文本, 序列耗尽后重复末条 ----

class FakeLlm final : public LlmProvider {
 public:
  PreparedLlmCall prepareCall(const LlmCallConfig& proposed) override {
    PreparedLlmCall prepared;
    prepared.config = proposed;
    return prepared;
  }

  LlmFinish stream(const LlmRequest&, const LlmStreamHandler& handler) override {
    std::string text;
    {
      std::lock_guard<std::mutex> lock(mtx);
      if (script.empty()) {
        text = "ok";
      } else if (nextIndex < script.size()) {
        text = script[nextIndex++];
      } else {
        text = script.back();
      }
    }
    handler.onChunk(StreamChunk{StreamBlockStart{0, "text"}});
    handler.onChunk(StreamChunk{StreamTextDelta{0, text}});
    handler.onChunk(
        StreamChunk{StreamBlockEnd{0, ContentBlock{TextBlock{text}}}});
    handler.onChunk(StreamChunk{StreamFinish{FinishStop{}}});
    LlmFinish finish;
    return finish;
  }

  // 依次吐出的响应; 空表 = 永远 "ok"。
  std::vector<std::string> script;

 private:
  std::mutex mtx;
  size_t nextIndex = 0;
};

// ---- 测试装置: 临时目录 + Lead 驱动 + TeamService ----
//
// 析构顺序即真实关闭顺序: dispose team (停队友) -> shutdown lead (join 驱动) ->
// 摘 writer。remove_all 用 error_code 版: 清理失败不该让测试进程抛异常。
struct TeamHarness {
  fs::path root;
  std::string sessionRoot;
  SystemPrompt systemPrompt;
  ToolRuntime tools;
  AgentExtensionPoints points;
  FakeLlm llm;
  std::unique_ptr<SessionWriter> leadWriter;
  std::unique_ptr<ReactLoopAgent> lead;
  std::unique_ptr<TeamService> team;

  explicit TeamHarness(const std::string& name,
                       TeamConfig config = TeamConfig{}) {
    static std::atomic<int> serial{0};
    root = fs::temp_directory_path()
           / ("avox-teamtest-" + name + "-"
              + std::to_string(serial.fetch_add(1)));
    fs::create_directories(root);
    sessionRoot = (root / "sessions").string();

    SessionHeader header;
    header.id = SessionId("session-lead-" + name);
    header.createdAt = static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
    auto session =
        std::make_unique<Session>(header.id, std::vector<SessionEvent>{},
                                  header);
    AgentOptions options;
    options.provider = "fake";
    // 驱动要求 provider/model 双非空才发请求 (ReactLoopAgent 构建 LLM 调用时校验),
    // 脚本假件也需要一个 model 名占位。
    options.model = "fake-model";
    lead = std::make_unique<ReactLoopAgent>(std::move(session), options,
                                            agentDeps());
    const std::string leadPath =
        dshSessionLogPath(sessionRoot, std::nullopt, lead->id());
    fs::create_directories(fs::path(leadPath).parent_path());
    leadWriter = std::make_unique<SessionWriter>();
    if (!leadWriter->attach(lead->session(), leadPath)) {
      std::cout << "FAIL: lead 日志无法落盘: " << leadWriter->lastError()
                << std::endl;
      ++g_failures;
    }
    TeamRosterDeps rosterDeps;
    rosterDeps.lead = lead.get();
    rosterDeps.systemPrompt = &systemPrompt;
    rosterDeps.tools = &tools;
    rosterDeps.llm = &llm;
    rosterDeps.points = &points;
    rosterDeps.sessionRoot = sessionRoot;
    rosterDeps.leadOptions = options;
    rosterDeps.memberDelegationDepth = 1;
    team = std::make_unique<TeamService>(config, rosterDeps, *leadWriter);
    team->recover();
  }

  ~TeamHarness() {
    if (team) team->dispose();
    if (lead) lead->shutdown();
    if (leadWriter) leadWriter->detach();
    std::error_code ignored;
    fs::remove_all(root, ignored);
  }

  TeamHarness(const TeamHarness&) = delete;
  TeamHarness& operator=(const TeamHarness&) = delete;

  // 排一轮 Lead turn 并等到静止 (FakeLlm 即时返回, 毫秒级)。
  void runLeadTurn(const std::string& text) {
    UserMessage message;
    message.id =
        MessageId(lead->id().value + "/user-" + std::to_string(turns++));
    message.content.push_back(TextBlock{text});
    message.source = userSource();
    lead->followup(std::move(message));
    CHECK(lead->whenIdle(30000));
  }

  SpawnTeammateRequest spawnRequest(const std::string& name,
                                    const std::string& prompt) {
    SpawnTeammateRequest request;
    request.name = name;
    request.description = "test member";
    request.prompt.push_back(ContentBlock{TextBlock{prompt}});
    return request;
  }

 private:
  ReactLoopAgent::Deps agentDeps() {
    ReactLoopAgent::Deps deps;
    deps.systemPrompt = &systemPrompt;
    deps.tools = &tools;
    deps.llm = &llm;
    deps.points = &points;
    return deps;
  }

  int turns = 0;
};

// fold 测试用的事件构造 (seq = 下标, 与日志契约一致)。
SessionEvent teamEvent(size_t seq, EventData data) {
  SessionEvent event;
  event.type = eventTypeOf(data);
  event.seq = seq;
  event.timeMs = 0;
  event.data = std::move(data);
  return event;
}

TeamMemberSnapshot memberSnapshot(const std::string& id, const std::string& name,
                                  TeamMemberPhase phase) {
  TeamMemberSnapshot snapshot;
  snapshot.id = SessionId(id);
  snapshot.name = name;
  snapshot.provider = "fake";
  snapshot.phase = phase;
  return snapshot;
}

TeamTaskSnapshot taskSnapshot(const std::string& id, int revision,
                              TeamTaskStatus status) {
  TeamTaskSnapshot snapshot;
  snapshot.id = id;
  snapshot.revision = revision;
  snapshot.subject = "s-" + id;
  snapshot.status = status;
  return snapshot;
}

// ---- 1. 折叠不变式 ----

void testFoldInvariants() {
  const SessionId root("session-root");
  const SessionId other("session-other");

  // 正常序列: provisioning -> active; task-1 rev1 -> rev2; msg-1 queued+delivered。
  {
    std::vector<SessionEvent> events;
    events.push_back(teamEvent(
        0, TeamMemberEventData{
               TEAM_EVENT_VERSION, root,
               memberSnapshot("session-m1", "worker",
                              TeamMemberPhase::Provisioning)}));
    events.push_back(teamEvent(
        1, TeamMemberEventData{
               TEAM_EVENT_VERSION, root,
               memberSnapshot("session-m1", "worker",
                              TeamMemberPhase::Active)}));
    events.push_back(teamEvent(
        2, TeamTaskEventData{
               TEAM_EVENT_VERSION, root,
               taskSnapshot("task-1", 1, TeamTaskStatus::Pending)}));
    TeamTaskSnapshot claimed =
        taskSnapshot("task-1", 2, TeamTaskStatus::InProgress);
    claimed.ownerId = SessionId("session-m1");
    events.push_back(
        teamEvent(3, TeamTaskEventData{TEAM_EVENT_VERSION, root, claimed}));
    TeamMessageSnapshot message;
    message.id = "msg-1";
    message.senderId = root;
    message.senderName = "lead";
    message.targetId = SessionId("session-m1");
    message.content.push_back(ContentBlock{TextBlock{"hello"}});
    events.push_back(
        teamEvent(4, TeamMessageQueuedData{TEAM_EVENT_VERSION, root, message}));
    events.push_back(teamEvent(
        5, TeamMessageDeliveredData{TEAM_EVENT_VERSION, root,
                                    std::string("msg-1"),
                                    SessionId("session-m1")}));

    const TeamFoldState state = foldTeam(root, events);
    CHECK_EQ(state.members.size(), static_cast<size_t>(1));
    CHECK(state.members[0].phase == TeamMemberPhase::Active);
    CHECK_EQ(state.tasks.size(), static_cast<size_t>(1));
    CHECK_EQ(state.tasks[0].revision, 2);
    CHECK(state.delivered.count("msg-1") == 1);
    CHECK_EQ(state.nextTaskNumber, static_cast<int64_t>(2));
    CHECK_EQ(state.nextMessageNumber, static_cast<int64_t>(2));
  }

  // teamId 不符的 team/* 事件整条忽略 (fork 前缀继承的别队状态)。
  {
    std::vector<SessionEvent> events;
    events.push_back(teamEvent(
        0, TeamMemberEventData{
               TEAM_EVENT_VERSION, other,
               memberSnapshot("session-x", "ghost",
                              TeamMemberPhase::Provisioning)}));
    const TeamFoldState state = foldTeam(root, events);
    CHECK(state.members.empty());
    CHECK_EQ(state.nextTaskNumber, static_cast<int64_t>(1));
  }

  // 名字永不复用。
  CHECK_THROWS((foldTeam(root, std::vector<SessionEvent>{
                                   teamEvent(0, TeamMemberEventData{
                                                    TEAM_EVENT_VERSION, root,
                                                    memberSnapshot(
                                                        "session-m1", "worker",
                                                        TeamMemberPhase::
                                                            Active)}),
                                   teamEvent(1, TeamMemberEventData{
                                                    TEAM_EVENT_VERSION, root,
                                                    memberSnapshot(
                                                        "session-m2", "worker",
                                                        TeamMemberPhase::
                                                            Provisioning)}),
                               })));

  // phase 只能 provisioning 起步。
  CHECK_THROWS((foldTeam(root, std::vector<SessionEvent>{
                                   teamEvent(0, TeamMemberEventData{
                                                    TEAM_EVENT_VERSION, root,
                                                    memberSnapshot(
                                                        "session-m1", "worker",
                                                        TeamMemberPhase::
                                                            Active)}),
                               })));

  // phase 只能一次收敛 (failed 不得回 active)。
  CHECK_THROWS((foldTeam(root, std::vector<SessionEvent>{
                                   teamEvent(0, TeamMemberEventData{
                                                    TEAM_EVENT_VERSION, root,
                                                    memberSnapshot(
                                                        "session-m1", "worker",
                                                        TeamMemberPhase::
                                                            Provisioning)}),
                                   teamEvent(1, TeamMemberEventData{
                                                    TEAM_EVENT_VERSION, root,
                                                    memberSnapshot(
                                                        "session-m1", "worker",
                                                        TeamMemberPhase::
                                                            Failed)}),
                                   teamEvent(2, TeamMemberEventData{
                                                    TEAM_EVENT_VERSION, root,
                                                    memberSnapshot(
                                                        "session-m1", "worker",
                                                        TeamMemberPhase::
                                                            Active)}),
                               })));

  // 任务 revision 从 1 起。
  CHECK_THROWS((foldTeam(root, std::vector<SessionEvent>{
                                   teamEvent(0, TeamTaskEventData{
                                                    TEAM_EVENT_VERSION, root,
                                                    taskSnapshot(
                                                        "task-1", 2,
                                                        TeamTaskStatus::
                                                            Pending)}),
                               })));

  // delivered 要求先入队; 消息只入队一次。
  {
    TeamMessageSnapshot message;
    message.id = "msg-1";
    message.senderId = root;
    message.targetId = SessionId("session-m1");
    CHECK_THROWS((foldTeam(root, std::vector<SessionEvent>{
                                     teamEvent(0, TeamMessageDeliveredData{
                                                      TEAM_EVENT_VERSION, root,
                                                      std::string("msg-1"),
                                                      SessionId("session-m1")}),
                                 })));
    CHECK_THROWS((foldTeam(root, std::vector<SessionEvent>{
                                     teamEvent(0, TeamMessageQueuedData{
                                                      TEAM_EVENT_VERSION, root,
                                                      message}),
                                     teamEvent(1, TeamMessageQueuedData{
                                                      TEAM_EVENT_VERSION, root,
                                                      message}),
                                 })));
  }
}

// ---- 2. 入参校验 ----

void testValidation() {
  CHECK_TEAM_ERROR(checkTeamMemberName("Worker"), TEAM_INVALID_MEMBER_NAME);
  CHECK_TEAM_ERROR(checkTeamMemberName("lead"), TEAM_INVALID_MEMBER_NAME);
  CHECK_TEAM_ERROR(checkTeamMemberName("a-"), TEAM_INVALID_MEMBER_NAME);
  checkTeamMemberName("worker-2");
  checkTeamMemberName("w");
  CHECK_EQ(teamWriteScope("src\\avox_agent\\"), std::string("src/avox_agent"));
  CHECK_EQ(teamWriteScope("./docs"), std::string("docs"));
  CHECK_TEAM_ERROR(teamWriteScope("/abs"), TEAM_INVALID_WRITE_SCOPE);
  CHECK_TEAM_ERROR(teamWriteScope("a/../b"), TEAM_INVALID_WRITE_SCOPE);
}

// ---- 3. 任务板 CAS ----

void testTaskBoard() {
  TeamHarness harness("tasks");

  // 创建: revision 1, id 按序分配。
  CreateTeamTaskRequest create;
  create.subject = "调研 zlm 协议";
  create.description = "拉通 fmp4 直播链路";
  const TeamTaskSnapshot task1 = harness.team->createTask(create);
  CHECK_EQ(task1.id, std::string("task-1"));
  CHECK_EQ(task1.revision, 1);
  CHECK(task1.status == TeamTaskStatus::Pending);

  // blockedBy 引用不存在的任务。
  CreateTeamTaskRequest badDeps;
  badDeps.subject = "坏依赖";
  badDeps.blockedBy = std::vector<std::string>{"task-404"};
  CHECK_TEAM_ERROR(harness.team->createTask(badDeps), TEAM_TASK_NOT_FOUND);

  // 依赖任务: task-2 等 task-1。
  CreateTeamTaskRequest create2;
  create2.subject = "写测试";
  create2.blockedBy = std::vector<std::string>{"task-1"};
  const TeamTaskSnapshot task2 = harness.team->createTask(create2);
  CHECK_EQ(task2.id, std::string("task-2"));

  // 就绪度: task-1 pending 即就绪; task-2 被阻塞。
  {
    const std::vector<TeamTaskView> views = harness.team->listTasks();
    CHECK_EQ(views.size(), static_cast<size_t>(2));
    CHECK(views[0].ready);
    CHECK(!views[1].ready);
    CHECK_EQ(views[1].blockedBy.size(), static_cast<size_t>(1));
  }

  // 三个独立身份: worker / other 各自一个真 agent (不跑轮次, 只作身份), lead 用
  // Lead 自身 —— 否则 owner 判定会因 id 相同而互相放行。
  ReactLoopAgent::Deps memberDeps;
  memberDeps.systemPrompt = &harness.systemPrompt;
  memberDeps.tools = &harness.tools;
  memberDeps.llm = &harness.llm;
  memberDeps.points = &harness.points;
  ReactLoopAgent workerAgent(
      std::make_unique<Session>(SessionId("session-board-worker")),
      AgentOptions{}, memberDeps);
  ReactLoopAgent otherAgent(
      std::make_unique<Session>(SessionId("session-board-other")),
      AgentOptions{}, memberDeps);
  TeamMembership worker;
  worker.root = &workerAgent;
  worker.role = TeamRole::Teammate;
  worker.name = "worker";
  TeamMembership other;
  other.root = &otherAgent;
  other.role = TeamRole::Teammate;
  other.name = "other";
  TeamMembership leadRole;
  leadRole.root = harness.lead.get();
  leadRole.role = TeamRole::Lead;
  leadRole.name = "lead";

  // CAS: 期望 revision 不符。
  UpdateTeamTaskRequest stale;
  stale.taskId = "task-1";
  stale.expectedRevision = 99;
  stale.action = TeamTaskAction::Claim;
  CHECK_TEAM_ERROR(harness.team->updateTask(worker, stale),
                   TEAM_TASK_STALE_REVISION);

  // claim: pending+无主+无未完依赖。
  UpdateTeamTaskRequest claim = stale;
  claim.expectedRevision = 1;
  const TeamTaskSnapshot claimed = harness.team->updateTask(worker, claim);
  CHECK(claimed.status == TeamTaskStatus::InProgress);
  CHECK(claimed.ownerId.has_value());
  CHECK_EQ(claimed.revision, 2);

  // 重复 claim / 阻塞 claim。
  UpdateTeamTaskRequest claimAgain = claim;
  claimAgain.expectedRevision = 2;
  CHECK_TEAM_ERROR(harness.team->updateTask(worker, claimAgain),
                   TEAM_TASK_ALREADY_CLAIMED);
  UpdateTeamTaskRequest claimBlocked;
  claimBlocked.taskId = "task-2";
  claimBlocked.expectedRevision = 1;
  claimBlocked.action = TeamTaskAction::Claim;
  CHECK_TEAM_ERROR(harness.team->updateTask(worker, claimBlocked),
                   TEAM_TASK_BLOCKED);

  // 归属: 他人 release 被拒; owner complete 通过。
  UpdateTeamTaskRequest release;
  release.taskId = "task-1";
  release.expectedRevision = 2;
  release.action = TeamTaskAction::Release;
  CHECK_TEAM_ERROR(harness.team->updateTask(other, release),
                   TEAM_TASK_UNAUTHORIZED);
  UpdateTeamTaskRequest complete = release;
  complete.action = TeamTaskAction::Complete;
  CHECK(harness.team->updateTask(worker, complete).status
        == TeamTaskStatus::Completed);

  // 依赖闭合: task-1 完成后 task-2 就绪, 可认领。
  UpdateTeamTaskRequest claim2;
  claim2.taskId = "task-2";
  claim2.expectedRevision = 1;
  claim2.action = TeamTaskAction::Claim;
  CHECK(harness.team->updateTask(worker, claim2).status
        == TeamTaskStatus::InProgress);

  // 依赖成环: task-1 (重开) 反向依赖 task-2。
  UpdateTeamTaskRequest reopen;
  reopen.taskId = "task-1";
  reopen.expectedRevision = 3;
  reopen.action = TeamTaskAction::Reopen;
  CHECK(harness.team->updateTask(worker, reopen).status
        == TeamTaskStatus::Pending);
  UpdateTeamTaskRequest cycle;
  cycle.taskId = "task-1";
  cycle.expectedRevision = 4;
  cycle.action = TeamTaskAction::SetDependencies;
  cycle.blockedBy = std::vector<std::string>{"task-2"};
  CHECK_TEAM_ERROR(harness.team->updateTask(worker, cycle),
                   TEAM_TASK_DEPENDENCY_CYCLE);

  // 有依赖者不可删: task-3 等 task-2, 删 task-2 被拒。
  CreateTeamTaskRequest create3;
  create3.subject = "下游";
  create3.blockedBy = std::vector<std::string>{"task-2"};
  CHECK_EQ(harness.team->createTask(create3).id, std::string("task-3"));
  UpdateTeamTaskRequest del;
  del.taskId = "task-2";
  del.expectedRevision = 2;
  del.action = TeamTaskAction::Delete;
  CHECK_TEAM_ERROR(harness.team->updateTask(worker, del),
                   TEAM_TASK_HAS_DEPENDENTS);

  // writeScopes: 名单外队友被拒, Lead 豁免。
  CreateTeamTaskRequest scoped;
  scoped.subject = "受限任务";
  scoped.writeScopes = std::vector<std::string>{"worker"};
  const TeamTaskSnapshot task4 = harness.team->createTask(scoped);
  UpdateTeamTaskRequest touch;
  touch.taskId = task4.id;
  touch.expectedRevision = 1;
  touch.action = TeamTaskAction::Edit;
  touch.subject = "改写";
  CHECK_TEAM_ERROR(harness.team->updateTask(other, touch),
                   TEAM_TASK_UNAUTHORIZED);
  CHECK_EQ(harness.team->updateTask(leadRole, touch).revision, 2);

  // reassign 到不存在的成员。
  UpdateTeamTaskRequest reassign;
  reassign.taskId = task4.id;
  reassign.expectedRevision = 2;
  reassign.action = TeamTaskAction::Reassign;
  reassign.owner = "ghost";
  CHECK_TEAM_ERROR(harness.team->updateTask(leadRole, reassign),
                   TEAM_MEMBER_NOT_FOUND);

  // get: 不存在。
  CHECK_TEAM_ERROR(harness.team->getTask("task-404"), TEAM_TASK_NOT_FOUND);

  workerAgent.shutdown();
  otherAgent.shutdown();
}

// ---- 4. continuable spawn ----

void testSpawn() {
  TeamHarness harness("spawn");

  // 坏名字 / 保留名。
  CHECK_TEAM_ERROR(
      harness.team->spawnTeammate(harness.spawnRequest("Bad", "x")),
      TEAM_INVALID_MEMBER_NAME);
  CHECK_TEAM_ERROR(
      harness.team->spawnTeammate(harness.spawnRequest("lead", "x")),
      TEAM_INVALID_MEMBER_NAME);

  // 正常起动: checkpoint 通过 (初始提示在子日志), 首轮跑完 (脚本 LLM)。
  const TeamMemberSnapshot worker =
      harness.team->spawnTeammate(harness.spawnRequest("worker", "去数星星"));
  CHECK(worker.phase == TeamMemberPhase::Active);
  CHECK(!worker.error.has_value());
  CHECK_EQ(worker.provider, std::string("fake"));

  // 子日志存在且含 assistant 消息。checkpoint 只等初始提示被记录 (首次模型请求前),
  // spawn 返回时首轮可能仍在跑 —— 先等静止再读盘。
  ReactLoopAgent* workerLive = harness.team->roster().findLive(worker.id);
  CHECK(workerLive != nullptr);
  CHECK(workerLive->whenIdle(30000));
  const std::string childPath =
      dshSessionLogPath(harness.sessionRoot, std::nullopt, worker.id);
  CHECK(fs::exists(childPath));
  {
    const LoadedSession loaded = loadSession(childPath);
    bool hasAssistant = false;
    for (const SessionEvent& event : loaded.events) {
      if (event.type == EventType::AssistantMessageEvent) hasAssistant = true;
    }
    CHECK(hasAssistant);
    // 头行血缘。
    CHECK(loaded.header.parentSession.has_value());
    CHECK_EQ(loaded.header.parentSession->value, harness.lead->id().value);
    CHECK_EQ(loaded.header.origin.value_or(""), std::string("subagent"));
  }

  // 名字永不复用。
  CHECK_TEAM_ERROR(
      harness.team->spawnTeammate(harness.spawnRequest("worker", "x")),
      TEAM_MEMBER_NAME_TAKEN);

  // 名册: Lead 行 + 队友行。
  {
    const std::vector<TeamMemberView> views = harness.team->listMembers();
    CHECK_EQ(views.size(), static_cast<size_t>(2));
    CHECK_EQ(views[0].name, std::string("lead"));
    CHECK(views[0].role == TeamRole::Lead);
    CHECK_EQ(views[1].name, std::string("worker"));
    // 首轮在跑或刚结束: 先等静止再断言 idle。
    ReactLoopAgent* live = harness.team->roster().findLive(worker.id);
    CHECK(live != nullptr);
    CHECK(live->whenIdle(30000));
    CHECK(harness.team->listMembers()[1].status
          == TeamMemberRuntimeStatus::Idle);
  }

  // 归属解析: 队友驱动是队友身份; Lead 是 lead; 局外 agent 不是成员。
  {
    ReactLoopAgent* live = harness.team->roster().findLive(worker.id);
    const TeamMembership membership = harness.team->membershipOf(live);
    CHECK(membership.role == TeamRole::Teammate);
    CHECK_EQ(membership.name, std::string("worker"));
    CHECK(harness.team->membershipOf(harness.lead.get()).role == TeamRole::Lead);

    ReactLoopAgent::Deps outsiderDeps;
    outsiderDeps.systemPrompt = &harness.systemPrompt;
    outsiderDeps.tools = &harness.tools;
    outsiderDeps.llm = &harness.llm;
    outsiderDeps.points = &harness.points;
    ReactLoopAgent outsider(
        std::make_unique<Session>(SessionId("session-outsider")),
        AgentOptions{}, outsiderDeps);
    CHECK_TEAM_ERROR(harness.team->membershipOf(&outsider),
                     TEAM_MEMBER_NOT_FOUND);
    outsider.shutdown();
  }

  // fork 上下文: Lead 先跑一轮, fork 队友继承前缀。
  harness.runLeadTurn("记住暗号: 蓝鲸");
  SpawnTeammateRequest forkRequest =
      harness.spawnRequest("forked", "接着聊");
  forkRequest.context = TeamMemberContext::Fork;
  const TeamMemberSnapshot forked = harness.team->spawnTeammate(forkRequest);
  CHECK(forked.phase == TeamMemberPhase::Active);
  {
    const std::string forkPath =
        dshSessionLogPath(harness.sessionRoot, std::nullopt, forked.id);
    const LoadedSession loaded = loadSession(forkPath);
    CHECK(loaded.header.seedLength.has_value());
    CHECK(*loaded.header.seedLength > 0);
    // 种子里带着 Lead 的那条用户消息。
    bool sawLeadText = false;
    for (const SessionEvent& event : loaded.events) {
      if (event.type != EventType::UserMessageEvent) continue;
      const UserMessageData& data = std::get<UserMessageData>(event.data);
      for (const ContentBlock& block : data.message.content) {
        if (const TextBlock* text = std::get_if<TextBlock>(&block)) {
          if (text->text.find("蓝鲸") != std::string::npos) sawLeadText = true;
        }
      }
    }
    CHECK(sawLeadText);
  }

  // interrupt: 活成员可中断 (空闲时 cancel 是空操作, 不抛); lead 不可被中断。
  const InterruptResult interrupted = harness.team->interruptAgent(
      harness.team->membershipOf(harness.lead.get()), "worker");
  CHECK(interrupted.previousStatus == TeamMemberRuntimeStatus::Idle);
  CHECK_TEAM_ERROR(
      harness.team->interruptAgent(
          harness.team->membershipOf(harness.lead.get()), "lead"),
      TEAM_LEAD_REQUIRED);
}

// ---- 5. 持久邮箱 ----

void testMailbox() {
  TeamConfig config;
  config.maxPendingMessagesPerMember = 2;
  config.maxMessageBytes = 64;
  TeamHarness harness("mailbox", config);
  const TeamMemberSnapshot worker =
      harness.team->spawnTeammate(harness.spawnRequest("worker", "待命"));
  CHECK(worker.phase == TeamMemberPhase::Active);

  SendTeamMessageRequest request;
  request.content.push_back(ContentBlock{TextBlock{"第一条"}});

  // 自发送 / 未知目标。
  request.target = "lead";
  CHECK_TEAM_ERROR(harness.team->sendMessage(*harness.lead, request),
                   TEAM_SELF_MESSAGE);
  request.target = "ghost";
  CHECK_TEAM_ERROR(harness.team->sendMessage(*harness.lead, request),
                   TEAM_INVALID_TARGET);

  // 活成员: quiet 也立即投递, 投递帧携 dsh 框头, 目标日志可回放。
  request.target = "worker";
  const SendTeamMessageResult live =
      harness.team->sendMessage(*harness.lead, request);
  CHECK(live.accepted);
  CHECK_EQ(live.messageId, std::string("msg-1"));
  {
    ReactLoopAgent* liveWorker = harness.team->roster().findLive(worker.id);
    CHECK(liveWorker != nullptr);
    CHECK(liveWorker->whenIdle(30000));
    // quiet 不唤醒: 帧停在 inbox (InboxSpliced), 目标下一轮才排干成 UserMessage。
    // 两种事件形态都算见帧 (与域层 deliveredTo 同规则)。
    bool sawFrame = false;
    liveWorker->withSession([&](Session& session) {
      for (const SessionEvent& event : session.events()) {
        if (event.type == EventType::InboxSpliced) {
          const InboxSplicedData& data = std::get<InboxSplicedData>(event.data);
          for (const UserMessage& inserted : data.inserted) {
            if (inserted.source.kind == MessageSourceKind::TeamMessage
                && inserted.source.messageId.value_or("") == "msg-1") {
              sawFrame = true;
            }
          }
        } else if (event.type == EventType::UserMessageEvent) {
          const UserMessageData& data = std::get<UserMessageData>(event.data);
          if (data.message.source.kind == MessageSourceKind::TeamMessage
              && data.message.id.value == "msg-1") {
            sawFrame = true;
          }
        }
      }
    });
    CHECK(sawFrame);
  }

  // 停掉成员 -> quiet 只排队不投递 (惰性: 不冷恢复)。
  harness.team->dispose();
  request.content.clear();
  request.content.push_back(ContentBlock{TextBlock{"第二条"}});
  const SendTeamMessageResult queued =
      harness.team->sendMessage(*harness.lead, request);
  CHECK(!queued.accepted);
  CHECK_EQ(queued.messageId, std::string("msg-2"));

  // wakeup: 惰性冷恢复 (从子日志重建驱动) 后投递。
  request.delivery = TeamMessageDelivery::Wakeup;
  request.content.clear();
  request.content.push_back(ContentBlock{TextBlock{"第三条"}});
  const SendTeamMessageResult wakeup =
      harness.team->sendMessage(*harness.lead, request);
  CHECK(wakeup.accepted);

  // 邮箱上限: 排队 (queued - delivered) 超 maxPendingMessagesPerMember。
  // 此刻 msg-2 仍排队 (quiet 到 Inactive 成员未投), 占 1 坑; 再排 1 条到 2 满。
  harness.team->dispose();
  request.delivery = TeamMessageDelivery::Quiet;
  request.content.clear();
  request.content.push_back(ContentBlock{TextBlock{"积压"}});
  CHECK(!harness.team->sendMessage(*harness.lead, request).accepted);
  request.content.clear();
  request.content.push_back(ContentBlock{TextBlock{"溢出"}});
  CHECK_TEAM_ERROR(harness.team->sendMessage(*harness.lead, request),
                   TEAM_MAILBOX_FULL);

  // 帧大小上限 (含框头的完整帧)。
  request.content.clear();
  request.content.push_back(ContentBlock{TextBlock{
      "这条消息比 maxMessageBytes=64 长得多得多得多得多得多得多得多得多"}});
  CHECK_TEAM_ERROR(harness.team->sendMessage(*harness.lead, request),
                   TEAM_MESSAGE_TOO_LARGE);
}

// ---- 6. 恢复语义 (进程重启模拟) ----

// 一套装配设施 (两轮共用类型): 崩溃前 / 重启后。
struct RecoveryRig {
  SystemPrompt systemPrompt;
  ToolRuntime tools;
  AgentExtensionPoints points;
  FakeLlm llm;
  std::unique_ptr<ReactLoopAgent> lead;
  std::unique_ptr<SessionWriter> writer;
  std::unique_ptr<TeamService> team;

  // fresh: 全新空会话; resume: 从磁盘日志重建。
  // (构造函数私有, make_unique 够不着 —— 静态工厂内直接 new 交 unique_ptr 接管)
  static std::unique_ptr<RecoveryRig> fresh(const std::string& sessionRoot,
                                            const SessionId& leadId) {
    return std::unique_ptr<RecoveryRig>(new RecoveryRig(sessionRoot, leadId, nullptr));
  }
  static std::unique_ptr<RecoveryRig> resume(const std::string& sessionRoot,
                                             const SessionId& leadId) {
    return std::unique_ptr<RecoveryRig>(new RecoveryRig(sessionRoot, leadId, &leadId));
  }

  ~RecoveryRig() {
    if (team) team->dispose();
    if (lead) lead->shutdown();
    if (writer) writer->detach();
  }

  RecoveryRig(const RecoveryRig&) = delete;
  RecoveryRig& operator=(const RecoveryRig&) = delete;

 private:
  RecoveryRig(const std::string& sessionRoot, const SessionId& leadId,
              const SessionId* resumeFrom)
      : lead([&]() -> std::unique_ptr<ReactLoopAgent> {
          ReactLoopAgent::Deps deps;
          deps.systemPrompt = &systemPrompt;
          deps.tools = &tools;
          deps.llm = &llm;
          deps.points = &points;
          AgentOptions options;
          options.provider = "fake";
          if (resumeFrom == nullptr) {
            return std::make_unique<ReactLoopAgent>(
                std::make_unique<Session>(leadId), options, deps);
          }
          const LoadedSession loaded =
              loadSession(dshSessionLogPath(sessionRoot, std::nullopt, leadId));
          return std::make_unique<ReactLoopAgent>(
              std::make_unique<Session>(loaded.header.id, loaded.events,
                                        loaded.header),
              options, deps);
        }()) {
    const std::string leadPath =
        dshSessionLogPath(sessionRoot, std::nullopt, leadId);
    fs::create_directories(fs::path(leadPath).parent_path());
    writer = std::make_unique<SessionWriter>();
    writer->attach(lead->session(), leadPath);
    TeamRosterDeps rosterDeps;
    rosterDeps.lead = lead.get();
    rosterDeps.systemPrompt = &systemPrompt;
    rosterDeps.tools = &tools;
    rosterDeps.llm = &llm;
    rosterDeps.points = &points;
    rosterDeps.sessionRoot = sessionRoot;
    rosterDeps.leadOptions = lead->options();
    rosterDeps.memberDelegationDepth = 1;
    team = std::make_unique<TeamService>(TeamConfig{}, rosterDeps, *writer);
    team->recover();
  }
};

void testRecovery() {
  const fs::path root = fs::temp_directory_path() / "avox-teamtest-recovery";
  std::error_code ignored;
  fs::remove_all(root, ignored);
  fs::create_directories(root);
  const std::string sessionRoot = (root / "sessions").string();
  const SessionId leadId("session-lead-recovery");
  std::string workerId;

  // ---- 崩溃前: 起队伍, 留下持久状态 ----
  {
    auto rig = RecoveryRig::fresh(sessionRoot, leadId);

    // 活队友 + 一条已投递消息 + 一条排队消息 (成员停后 quiet 发)。
    SpawnTeammateRequest spawn;
    spawn.name = "worker";
    spawn.description = "恢复测试";
    spawn.prompt.push_back(ContentBlock{TextBlock{"待命"}});
    const TeamMemberSnapshot worker = rig->team->spawnTeammate(spawn);
    CHECK(worker.phase == TeamMemberPhase::Active);
    workerId = worker.id.value;

    SendTeamMessageRequest delivered;
    delivered.target = "worker";
    delivered.content.push_back(ContentBlock{TextBlock{"已投递"}});
    CHECK(rig->team->sendMessage(*rig->lead, delivered).accepted);

    rig->team->dispose();
    SendTeamMessageRequest pending;
    pending.target = "worker";
    pending.content.push_back(ContentBlock{TextBlock{"排队中"}});
    CHECK(!rig->team->sendMessage(*rig->lead, pending).accepted);

    // 手工塞一个 provisioning 残留 (子日志缺失): 模拟崩在 spawn 段间。
    rig->team->journal().transact([&]() {
      TeamMemberSnapshot residue = memberSnapshot(
          "session-never-started", "leftover", TeamMemberPhase::Provisioning);
      rig->team->journal().appendAndFlush(
          TeamMemberEventData{TEAM_EVENT_VERSION, leadId, residue});
    });
    // 析构模拟崩溃后进程退出: dispose 已做过, 剩 shutdown + detach (= 落盘)。
  }

  // ---- 重启后: 全新进程视角, 只认磁盘 ----
  {
    auto rig = RecoveryRig::resume(sessionRoot, leadId);

    // 重折叠: 名册完整; worker 仍 active 且 Inactive (惰性, 未起驱动);
    // provisioning 残留被冷裁定为 failed。
    {
      const std::vector<TeamMemberView> views = rig->team->listMembers();
      CHECK_EQ(views.size(), static_cast<size_t>(3));
      bool sawWorker = false;
      bool sawLeftover = false;
      for (const TeamMemberView& view : views) {
        if (view.name == "worker") {
          sawWorker = true;
          CHECK(view.status == TeamMemberRuntimeStatus::Inactive);
        }
        if (view.name == "leftover") {
          sawLeftover = true;
          CHECK(view.status == TeamMemberRuntimeStatus::Failed);
          CHECK(!view.diagnostics.empty());
        }
      }
      CHECK(sawWorker);
      CHECK(sawLeftover);
    }

    // 冷裁定已落盘: 再恢复一次仍收敛 (failed 不回 provisioning)。
    rig->team->recover();
    for (const TeamMemberView& view : rig->team->listMembers()) {
      if (view.name == "leftover") {
        CHECK(view.status == TeamMemberRuntimeStatus::Failed);
      }
    }

    // 邮箱: quiet 补投不叫醒 Inactive 成员 —— 排队件仍在。
    rig->team->journal().readState([&](const TeamFoldState& state) {
      size_t pendingForWorker = 0;
      for (const TeamMessageSnapshot& message : state.queued) {
        if (state.delivered.count(message.id) == 0
            && message.targetId.value == workerId) {
          ++pendingForWorker;
        }
      }
      CHECK_EQ(pendingForWorker, static_cast<size_t>(1));
    });

    // wakeup 投递: 惰性冷恢复 (worker 子日志在) + 本条投递成功。
    SendTeamMessageRequest wake;
    wake.target = "worker";
    wake.delivery = TeamMessageDelivery::Wakeup;
    wake.content.push_back(ContentBlock{TextBlock{"醒醒"}});
    CHECK(rig->team->sendMessage(*rig->lead, wake).accepted);

    // 名字永不复用跨重启。
    SpawnTeammateRequest reuse;
    reuse.name = "worker";
    reuse.description = "重名";
    reuse.prompt.push_back(ContentBlock{TextBlock{"x"}});
    CHECK_TEAM_ERROR(rig->team->spawnTeammate(reuse), TEAM_MEMBER_NAME_TAKEN);
  }

  fs::remove_all(root, ignored);
}

}  // namespace

int main() {
  auto mark = [](const char* next) {
    std::cout << "-- " << next << std::endl;
  };
  testFoldInvariants();
  mark("validation");
  testValidation();
  mark("taskboard");
  testTaskBoard();
  mark("spawn");
  testSpawn();
  mark("mailbox");
  testMailbox();
  mark("recovery");
  testRecovery();
  mark("done");
  if (g_failures == 0) {
    std::cout << "teamtest: 全部通过" << std::endl;
    return 0;
  }
  std::cout << "teamtest: " << g_failures << " 处失败" << std::endl;
  return 1;
}
