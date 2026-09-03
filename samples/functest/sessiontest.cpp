// 会话事件日志的不变式自测。
//
// 覆盖 avox_agent/core 的关键契约。编译通过不等于行为正确 —— 尤其是 surface 投影与 replace
// 校验那几百行, 它们的错误不会在编译期暴露, 而会在「模型看到的历史悄悄错了」这种最难查的
// 地方暴露。
//
// dsh 互通部分 (布局/头行/分片/打包行/墓碑/附件/真实日志装载) 与 dsh 仓的
// format.ts / chunk-rows.ts / 真实 snapshot 逐字对照: 这里的每条断言都是
// 「avox 产的日志 dsh 读得懂, dsh 产的日志 avox 读得懂」的一块证据。

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "avox/AvoxImage.h"
#include "avox/module/Json.hpp"
#include "avox/module/Sha256.hpp"
#include "avox_agent/adapter/DshAttachmentStore.hpp"
#include "avox_agent/core/DshLayout.hpp"
#include "avox_agent/policy/ModelRoutePolicy.hpp"
#include "avox_agent/provider/ChatProvider.hpp"
#include "avox_agent/core/Llm.hpp"
#include "avox_agent/core/Session.hpp"
#include "avox_agent/core/SessionCodec.hpp"
#include "avox_agent/core/SessionPersistence.hpp"

using namespace avox;

namespace {

namespace fs = std::filesystem;

int g_failures = 0;

#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      std::cout << "FAIL line " << __LINE__ << ": " << #cond << std::endl; \
      ++g_failures;                                                       \
    }                                                                     \
  } while (0)

#define CHECK_EQ(actual, expected)                                            \
  do {                                                                        \
    const auto actualValue = (actual);                                        \
    const auto expectedValue = (expected);                                    \
    if (!(actualValue == expectedValue)) {                                    \
      std::cout << "FAIL line " << __LINE__ << ": " << #actual << " = "       \
                << actualValue << ", 期望 " << expectedValue << std::endl;    \
      ++g_failures;                                                           \
    }                                                                         \
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

// 抛出的异常消息须含 needle (限额用例靠它核对 dsh 错误码字样与恢复指引)。
#define CHECK_THROWS_WITH(expr, needle)                                       \
  do {                                                                        \
    bool threw = false;                                                       \
    std::string what;                                                         \
    try {                                                                     \
      expr;                                                                   \
    } catch (const std::exception& e) {                                       \
      threw = true;                                                           \
      what = e.what();                                                        \
    }                                                                         \
    if (!threw || what.find(needle) == std::string::npos) {                   \
      std::cout << "FAIL line " << __LINE__ << ": 期望抛含 \"" << needle      \
                << "\" 的异常: " << #expr << " (实际 "                         \
                << (threw ? "\"" + what + "\"" : std::string("未抛")) << ")"  \
                << std::endl;                                                 \
      ++g_failures;                                                           \
    }                                                                         \
  } while (0)

UserMessage userMessage(const std::string& id, const std::string& text,
                        MessageSource source = userSource()) {
  UserMessage message;
  message.id = MessageId(id);
  message.content.push_back(TextBlock{text});
  message.source = std::move(source);
  return message;
}

AssistantMessage assistantMessage(const std::string& text) {
  AssistantMessage message;
  if (!text.empty()) message.content.push_back(TextBlock{text});
  message.id = MessageId("a-" + text);
  // dsh 校验: assistant 消息的 provider/model 在 source 内且非空。
  message.source = modelSource("test", "test-model");
  return message;
}

ToolResultData toolResult(int turn, int step, const CallId& callId,
                          const std::string& text, bool isError) {
  ToolResultMessage message;
  message.id = MessageId("r-" + callId.value);
  message.source = toolSource(callId);
  ToolResultBlock block;
  block.toolCallId = callId;
  block.content.push_back(TextBlock{text});
  block.isError = isError;
  message.content.push_back(std::move(block));
  ToolResultData data;
  data.turn = turn;
  data.step = step;
  data.message = std::move(message);
  return data;
}

SurfaceIntent appendIntent() {
  SurfaceIntent intent;
  intent.surfaceOp = SurfaceAppend{};
  return intent;
}

SurfaceIntent appendIntent(std::vector<size_t> sources) {
  SurfaceIntent intent;
  intent.surfaceOp = SurfaceAppend{};
  intent.sourceEventSeqs = std::move(sources);
  return intent;
}

SurfaceIntent replaceIntent(size_t start, size_t end,
                           std::vector<size_t> sources) {
  SurfaceIntent intent;
  intent.surfaceOp = SurfaceReplace{start, end};
  intent.sourceEventSeqs = std::move(sources);
  return intent;
}

// 整文件落盘 (二进制, LF 结尾) —— 与 SessionWriter 的物理写法一致。
void writeAllLines(const std::string& path,
                   const std::vector<std::string>& lines) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  for (const std::string& line : lines) {
    out.write(line.data(), static_cast<std::streamsize>(line.size()));
    out.put('\n');
  }
}

fs::path freshDir(const char* name) {
  const fs::path root = fs::temp_directory_path() / name;
  std::error_code ec;
  fs::remove_all(root, ec);
  fs::create_directories(root, ec);
  return root;
}

// ---------------------------------------------------------------------------

void testSeqContiguity() {
  Session session(SessionId("t1"));
  CHECK_EQ(session.seq(), size_t(0));
  CHECK_EQ(session.append(TurnStartData{1}), size_t(0));
  CHECK_EQ(session.append(StepStartData{1, 1}), size_t(1));
  CHECK_EQ(session.append(UserMessageData{userMessage("m1", "hi")}, appendIntent()),
           size_t(2));
  CHECK_EQ(session.seq(), size_t(3));
  for (size_t i = 0; i < session.events().size(); ++i) {
    CHECK_EQ(session.events()[i].seq, i);
  }
}

// 非 surface 事件不进模型历史 —— 这是「模型历史是日志的投影, 不是日志本身」的核心。
void testProjectionSkipsNonSurface() {
  Session session(SessionId("t2"));
  session.append(TurnStartData{1});
  session.append(StepStartData{1, 1});
  session.append(UserMessageData{userMessage("m1", "问题")}, appendIntent());
  session.append(AssistantChunkData{1, 1, StreamTextDelta{0, "答"}});
  session.append(AssistantChunkData{1, 1, StreamTextDelta{0, "案"}});
  session.append(AssistantMessageData{1, 1, assistantMessage("答案"), std::nullopt},
                 appendIntent({3, 4}));
  session.append(StepEndData{1, 1});
  session.append(TurnEndData{1, TurnEndCompleted{}});

  CHECK_EQ(session.events().size(), size_t(8));
  // 8 条事件, 只有 2 条进历史。
  CHECK_EQ(session.deriveMessages().size(), size_t(2));
  CHECK(std::holds_alternative<UserMessage>(session.deriveMessages()[0]));
  CHECK(std::holds_alternative<AssistantMessage>(session.deriveMessages()[1]));
}

// 只承载 usage 的空 assistant/message 不该往历史里塞一条无内容的助手轮次。
void testEmptyAssistantMessageSkipped() {
  Session session(SessionId("t3"));
  session.append(UserMessageData{userMessage("m1", "问题")}, appendIntent());
  TokenUsage usage;
  usage.inputTokens = 100;
  session.append(AssistantMessageData{1, 1, assistantMessage(""), usage},
                 appendIntent({}));
  CHECK_EQ(session.events().size(), size_t(2));
  CHECK_EQ(session.deriveMessages().size(), size_t(1));
}

// 压缩: 历史变短而事件一条不减。
void testCompactionReplaceShortensHistoryWithoutDeletingEvents() {
  Session session(SessionId("t4"));
  const size_t u1 = session.append(UserMessageData{userMessage("m1", "轮1提问")},
                                   appendIntent());
  const size_t a1 = session.append(
      AssistantMessageData{1, 1, assistantMessage("轮1回答"), std::nullopt},
      appendIntent({}));
  const size_t u2 = session.append(UserMessageData{userMessage("m2", "轮2提问")},
                                   appendIntent());
  const size_t a2 = session.append(
      AssistantMessageData{2, 1, assistantMessage("轮2回答"), std::nullopt},
      appendIntent({}));
  session.append(UserMessageData{userMessage("m3", "轮3提问")}, appendIntent());

  CHECK_EQ(session.deriveMessages().size(), size_t(5));
  const size_t eventsBefore = session.events().size();
  const size_t generationBefore = session.getSurface().replaceGeneration();

  // 用一条检查点消息替换前四个节点 (dsh 压缩检查点的 source 形状)。
  session.append(
      UserMessageData{userMessage("summary", "[历史已压缩] 纪要",
                                  compactCheckpointSource("cx-1"))},
      replaceIntent(u1, a2, {u1, a1, u2, a2}));

  // 历史: 摘要 + 轮3提问 = 2 条。
  CHECK_EQ(session.deriveMessages().size(), size_t(2));
  // 事件只增不减。
  CHECK_EQ(session.events().size(), eventsBefore + 1);
  CHECK_EQ(session.getSurface().replaceGeneration(), generationBefore + 1);
  // 被遮蔽的原始事件仍在日志里, 可审计可回放。
  CHECK_EQ(std::string(eventTypeName(session.events()[u1].type)),
           std::string("user/message"));
  CHECK_EQ(std::string(eventTypeName(session.events()[a1].type)),
           std::string("assistant/message"));
}

void testReplaceValidation() {
  Session session(SessionId("t5"));
  const size_t u1 = session.append(UserMessageData{userMessage("m1", "a")},
                                   appendIntent());
  const size_t a1 = session.append(
      AssistantMessageData{1, 1, assistantMessage("b"), std::nullopt},
      appendIntent({}));

  // 起点不在 surface 上 (指向一个不存在的 seq)。
  CHECK_THROWS(session.append(
      UserMessageData{userMessage("bad1", "x")},
      replaceIntent(999, a1, {999, a1})));

  // sourceEventSeqs 未覆盖全部被遮蔽节点。
  CHECK_THROWS(session.append(UserMessageData{userMessage("bad2", "x")},
                             replaceIntent(u1, a1, {u1})));

  // 引用了不更早的事件 (自己或未来)。
  CHECK_THROWS(session.append(UserMessageData{userMessage("bad3", "x")},
                             appendIntent({9999})));

  // 上面每次失败都不该改变日志。
  CHECK_EQ(session.events().size(), size_t(2));
  CHECK_EQ(session.deriveMessages().size(), size_t(2));
}

// tool/result 的 replace 只能改正文, 不能改身份或成败。
void testToolResultRewriteRestriction() {
  Session session(SessionId("t6"));
  session.append(UserMessageData{userMessage("m1", "跑个工具")}, appendIntent());
  const size_t callSeq = session.append(
      ToolCallData{1, 1, CallId("c1"), "grep", "{\"pattern\":\"err\"}"});

  const ToolResultData data = toolResult(1, 1, CallId("c1"), "很长的原始输出", false);
  const size_t resultSeq = session.append(data, appendIntent({callSeq}));

  // 合法: 只换正文。
  ToolResultData pruned = data;
  pruned.message.content[0].content.clear();
  pruned.message.content[0].content.push_back(TextBlock{"(已裁剪)"});
  session.append(pruned, replaceIntent(resultSeq, resultSeq, {resultSeq}));
  CHECK_EQ(session.deriveMessages().size(), size_t(2));

  // 非法: 改了成败标志。
  const std::vector<size_t>& nodes = session.getSurface().nodes();
  const size_t currentResultSeq = nodes.back();
  ToolResultData tampered = pruned;
  tampered.message.content[0].isError = true;
  CHECK_THROWS(session.append(
      tampered, replaceIntent(currentResultSeq, currentResultSeq, {currentResultSeq})));
}

void testCodecRoundTrip() {
  Session session(SessionId("t7"));
  session.append(TurnStartData{1});
  session.append(UserMessageData{userMessage("m1", "中文与 emoji 🎬 测试")},
                 appendIntent());
  ToolCallData call;
  call.turn = 1;
  call.step = 1;
  call.callId = CallId("c1");
  call.name = "read";
  call.arguments = "{\"path\":\"D:\\\\log\\\\a.txt\"}";
  session.append(call);
  session.append(TurnEndData{1, TurnEndAborted{CancelByHook{"预算耗尽"}}});
  // todo/write 原生往返: 整表快照 (wire data.todos), 与墓碑路径无关。
  TodoWriteData todo;
  todo.todos.push_back(TodoItem{"梳理日志格式差异", TodoStatus::Completed});
  todo.todos.push_back(TodoItem{"修复编码卫生", TodoStatus::InProgress});
  session.append(todo);

  for (const SessionEvent& original : session.events()) {
    const std::string line = encodeEvent(original);
    const DecodedEvent decoded = decodeEvent(line);
    CHECK(decoded.status == DecodedEvent::Status::Ok);
    CHECK_EQ(decoded.event.seq, original.seq);
    CHECK(decoded.event.type == original.type);
    CHECK_EQ(decoded.event.timeMs, original.timeMs);
    // 二次编码应当字节一致 —— 那才说明解码没丢信息。
    CHECK_EQ(encodeEvent(decoded.event), line);
  }
}

// 未识别的必需事件必须拒绝整个日志; 带 ignorable 的才可跳过。
void testUnknownEventTypeHandling() {
  CHECK_THROWS(decodeEvent(
      "{\"type\":\"future/thing\",\"seq\":0,\"time\":1,\"data\":{}}"));

  const DecodedEvent skipped = decodeEvent(
      "{\"type\":\"future/thing\",\"seq\":0,\"time\":1,\"data\":{},"
      "\"ignorable\":true}");
  CHECK(skipped.status == DecodedEvent::Status::SkippedIgnorable);
}

// resume 保真: 工具调用与结果在恢复后仍在派生历史里。
// 这是旧实现的核心 bug —— TrackRecorder::restoreContext 只恢复 userInput + finalContent。
void testResumePreservesToolHistory() {
  const fs::path root = freshDir("avox_session_test");
  const std::string path = (root / "resume.jsonl").string();

  size_t eventsBefore = 0;
  size_t messagesBefore = 0;
  {
    SessionHeader header;
    header.version = SESSION_FORMAT_VERSION;
    header.id = SessionId("resume");
    header.createdAt = 1;
    auto session = std::make_unique<Session>(SessionId("resume"),
                                             std::vector<SessionEvent>{}, header);
    SessionWriter writer;
    CHECK(writer.attach(*session, path));

    session->append(TurnStartData{1});
    session->append(StepStartData{1, 1});
    session->append(UserMessageData{userMessage("m1", "分析日志")}, appendIntent());
    session->append(
        AssistantMessageData{1, 1, assistantMessage("我来看看"), std::nullopt},
        appendIntent({}));
    const size_t callSeq = session->append(
        ToolCallData{1, 1, CallId("c1"), "grep", "{\"p\":\"err\"}"});
    session->append(toolResult(1, 1, CallId("c1"), "找到 3 处错误", false),
                    appendIntent({callSeq}));
    session->append(StepEndData{1, 1});
    session->append(TurnEndData{1, TurnEndCompleted{}});

    eventsBefore = session->events().size();
    messagesBefore = session->deriveMessages().size();
    writer.detach();
  }

  // 历史应有 3 条: user + assistant + tool result。
  CHECK_EQ(messagesBefore, size_t(3));

  LoadedSession loaded = loadSession(path);
  CHECK_EQ(loaded.events.size(), eventsBefore);
  CHECK(!loaded.repairedInterruptedTail);

  Session resumed(SessionId("resume"), std::move(loaded.events),
                  std::move(loaded.header));
  // 关键: 工具调用与结果在 resume 之后仍然可见。
  CHECK_EQ(resumed.deriveMessages().size(), size_t(3));
  bool foundToolResult = false;
  for (const Message& message : resumed.deriveMessages()) {
    if (std::holds_alternative<ToolResultMessage>(message)) foundToolResult = true;
  }
  CHECK(foundToolResult);
  // 构造应补一条 end-seed 标记 seed 边界。
  CHECK_EQ(resumed.events().size(), eventsBefore + 1);
  CHECK(resumed.events().back().type == EventType::SessionEndSeed);
  CHECK_EQ(resumed.getFirstLiveSeq(), eventsBefore);

  // attach 对齐: 补写内存里多出来的 end-seed, 且不重复。
  {
    SessionWriter writer;
    CHECK(writer.attach(resumed, path));
    writer.detach();
  }
  LoadedSession reloaded = loadSession(path);
  CHECK_EQ(reloaded.events.size(), eventsBefore + 1);

  // 再 attach 一次不应再长。
  {
    Session again(SessionId("resume"), std::move(reloaded.events),
                  std::move(reloaded.header));
    SessionWriter writer;
    CHECK(writer.attach(again, path));
    writer.detach();
  }
  LoadedSession thirdLoad = loadSession(path);
  CHECK_EQ(thirdLoad.events.size(), eventsBefore + 1);

  fs::remove_all(root);
}

// 崩溃遗留的未闭合 turn 应被补写 interrupted (先 step/end 再 turn/end)。
void testInterruptedTailRepair() {
  const fs::path root = freshDir("avox_session_test_crash");
  const std::string path = (root / "crash.jsonl").string();

  size_t eventsBefore = 0;
  {
    SessionHeader header;
    header.version = SESSION_FORMAT_VERSION;
    header.id = SessionId("crash");
    header.createdAt = 1;
    Session session(SessionId("crash"), std::vector<SessionEvent>{}, header);
    SessionWriter writer;
    CHECK(writer.attach(session, path));
    session.append(TurnStartData{1});
    session.append(StepStartData{1, 1});
    session.append(UserMessageData{userMessage("m1", "干活")}, appendIntent());
    // 到此「崩溃」: turn/end 与 step/end 都没写。
    eventsBefore = session.events().size();
    writer.detach();
  }

  LoadedSession loaded = loadSession(path);
  CHECK(loaded.repairedInterruptedTail);
  CHECK_EQ(loaded.events.size(), eventsBefore + 2);
  CHECK(loaded.events[eventsBefore].type == EventType::StepEnd);
  CHECK(loaded.events[eventsBefore + 1].type == EventType::TurnEnd);
  const auto& turnEnd = std::get<TurnEndData>(loaded.events[eventsBefore + 1].data);
  CHECK(std::holds_alternative<TurnEndInterrupted>(turnEnd.reason));

  fs::remove_all(root);
}

// header 折叠: 仅取最后一条。
void testRequestHeaderFold() {
  Session session(SessionId("t8"));
  CHECK(session.requestHeader() == nullptr);

  EpochHeader first;
  first.config.provider = "openai";
  first.config.model = "gpt-a";
  first.system = "系统提示";
  session.append(RequestHeaderData{first, RequestHeaderReason::Initial});
  CHECK(session.requestHeader() != nullptr);
  CHECK_EQ(session.requestHeader()->config.model, std::string("gpt-a"));

  EpochHeader second = first;
  second.config.model = "gpt-b";
  session.append(RequestHeaderData{second, RequestHeaderReason::Change});
  CHECK_EQ(session.requestHeader()->config.model, std::string("gpt-b"));
}

// seed 必须走与 append 相同的校验。
void testSeedValidation() {
  // seq 不连续。
  std::vector<SessionEvent> badSeed;
  SessionEvent event;
  event.type = EventType::TurnStart;
  event.seq = 5;
  event.timeMs = 1;
  event.data = TurnStartData{1};
  badSeed.push_back(event);
  CHECK_THROWS(Session(SessionId("bad"), badSeed));

  // 类型标签与载荷不符。
  std::vector<SessionEvent> mismatched;
  SessionEvent wrong;
  wrong.type = EventType::StepStart;
  wrong.seq = 0;
  wrong.timeMs = 1;
  wrong.data = TurnStartData{1};
  mismatched.push_back(wrong);
  CHECK_THROWS(Session(SessionId("bad2"), mismatched));
}

// ---------------------------------------------------------------------------
// dsh 互通
// ---------------------------------------------------------------------------

// 目录布局逐字节对齐 dsh format.ts: encodeSegment / projectKey / logPath。
void testDshLayout() {
  // encodeSegment: 安全字符直通, ~XXXX 转义按 UTF-16 码单元 (增补平面拆代理对)。
  CHECK_EQ(dshEncodeSegment("abc-1_2.3"), "abc-1_2.3");
  CHECK_EQ(dshEncodeSegment("."), "~002E");
  CHECK_EQ(dshEncodeSegment(".."), "~002E~002E");
  CHECK_EQ(dshEncodeSegment("a b"), "a~0020b");
  CHECK_EQ(dshEncodeSegment("a~b"), "a~007Eb");
  CHECK_EQ(dshEncodeSegment("中文"), "~4E2D~6587");
  // 🎬 = U+1F3AC = 代理对 D83C DFAC。
  CHECK_EQ(dshEncodeSegment("\xF0\x9F\x8E\xAC"), "~D83C~DFAC");
  CHECK_THROWS(dshEncodeSegment(""));

  // projectKey: 分隔符折叠成单个 '-', 去首部 '-', 空 -> root, --slug(≤251)--。
  CHECK_EQ(dshProjectKey("D:\\Work\\github\\avox"), "--D-Work-github-avox--");
  CHECK_EQ(dshProjectKey("/home/u/proj"), "--home-u-proj--");
  CHECK_EQ(dshProjectKey(":::"), "--root--");
  CHECK_EQ(dshProjectKey("C:\\x~~y"), "--C-x~007E~007Ey--");
  CHECK_THROWS(dshProjectKey(""));
  std::string longCwd(300, 'a');
  CHECK_EQ(dshProjectKey(longCwd).size(), size_t(255));

  // 目录树: <root>/<projectKey>/<encodeSegment(id)>/session.jsonl。
  const fs::path logFile(dshSessionLogPath("R", "D:\\Work\\github\\avox",
                                           SessionId("s-1")));
  // 4 个组件: root / projectKey / encodeSegment(id) / session.jsonl。
  const std::vector<fs::path> parts(logFile.begin(), logFile.end());
  CHECK_EQ(parts.size(), size_t(4));
  CHECK_EQ(parts[0].string(), std::string("R"));
  CHECK_EQ(parts[1].string(), std::string("--D-Work-github-avox--"));
  CHECK_EQ(parts[2].string(), std::string("s-1"));
  CHECK_EQ(parts[3].string(), std::string("session.jsonl"));
  // 无 cwd 的会话归置到 _no-cwd。
  const fs::path noCwd(dshProjectDir("R", std::nullopt));
  CHECK_EQ(noCwd.filename().string(), std::string("_no-cwd"));
}

// 头行形状: exact-key, delegationDepth 恒写, 退役字段拒收, 版本不符先拒。
void testDshHeaderShape() {
  SessionHeader header;
  header.version = SESSION_FORMAT_VERSION;
  header.id = SessionId("hdr-1");
  header.createdAt = 42;
  header.cwd = "D:\\w";
  const std::string line = encodeHeader(header);

  const Json j = parserJson(line.c_str());
  const Json::JsonObject& object = j.get<Json::JsonObject>();
  CHECK_EQ(object.size(), size_t(6));
  CHECK(object.find("type") != object.end());
  CHECK(object.find("version") != object.end());
  CHECK(object.find("id") != object.end());
  CHECK(object.find("createdAt") != object.end());
  CHECK(object.find("cwd") != object.end());
  // delegationDepth 无值也恒写 0 (dsh 头行校验必填)。
  CHECK(object.find("delegationDepth") != object.end());
  CHECK(j["delegationDepth"].bInt() && j["delegationDepth"].get<int64_t>() == 0);
  // 退役字段绝不写。
  CHECK(object.find("sandboxMode") == object.end());
  CHECK(object.find("approvalPolicy") == object.end());
  CHECK_EQ(j["type"].get<std::string>(), std::string("session"));

  // 读侧: 退役字段 / 版本不符 / 缺 delegationDepth 一律拒绝。
  CHECK_THROWS(decodeHeader(
      "{\"type\":\"session\",\"version\":0,\"id\":\"x\",\"createdAt\":1,"
      "\"delegationDepth\":0,\"sandboxMode\":\"danger\"}"));
  CHECK_THROWS(decodeHeader(
      "{\"type\":\"session\",\"version\":99,\"id\":\"x\",\"createdAt\":1,"
      "\"delegationDepth\":0}"));
  CHECK_THROWS(decodeHeader(
      "{\"type\":\"session\",\"version\":0,\"id\":\"x\",\"createdAt\":1}"));
}

// 7 种流分片的编解码往返 + BlockAssembler 重组 (dsh translate.ts 同构)。
void testDeltaChunkRoundTrip() {
  TokenUsage usage;
  usage.inputTokens = 100;
  usage.outputTokens = 20;
  usage.cacheReadTokens = 5;
  usage.reasoningTokens = 8;

  std::vector<StreamChunk> chunks{
      StreamBlockStart{0, "text"},
      StreamTextDelta{0, "你"},
      StreamTextDelta{0, "好"},
      StreamBlockStart{1, "reasoning"},
      StreamReasoningDelta{1, "想"},
      StreamToolCallDelta{2, CallId("c9"), std::string("grep"), "{}"},
      StreamToolCallDelta{2, CallId("c9"), std::nullopt, "{\"p\":1}"},
      StreamBlockEnd{1, ReasoningBlock{"想"}},
      StreamBlockEnd{0, TextBlock{"你好"}},
      StreamUsage{usage},
      StreamFinish{FinishStop{}},
      StreamFinish{FinishError{LlmFailure{"超时", "TIMEOUT", 504}}},
  };

  for (size_t i = 0; i < chunks.size(); ++i) {
    SessionEvent event;
    event.type = EventType::AssistantChunk;
    event.seq = i;
    event.timeMs = 100 + static_cast<int64_t>(i);
    event.data = AssistantChunkData{1, 1, chunks[i]};
    const std::string line = encodeEvent(event);
    const DecodedEvent decoded = decodeEvent(line);
    CHECK(decoded.status == DecodedEvent::Status::Ok);
    CHECK(decoded.event.type == EventType::AssistantChunk);
    const auto& data = std::get<AssistantChunkData>(decoded.event.data);
    CHECK(data.chunk.index() == chunks[i].index());
    CHECK_EQ(encodeEvent(decoded.event), line);
  }

  // 组装: 块按首见定序, delta 按索引归位, usage/finish 不进内容。
  BlockAssembler assembler;
  for (size_t i = 0; i < 9; ++i) assembler.push(chunks[i]);
  CHECK_EQ(assembler.blocks().size(), size_t(3));
  const auto* text = std::get_if<TextBlock>(&assembler.blocks()[0]);
  CHECK(text != nullptr && text->text == "你好");
  const auto* reasoning = std::get_if<ReasoningBlock>(&assembler.blocks()[1]);
  CHECK(reasoning != nullptr && reasoning->text == "想");
  assembler.push(StreamUsage{usage});
  assembler.push(StreamFinish{FinishToolCalls{}});
  CHECK_EQ(assembler.blocks().size(), size_t(3));
  CHECK(assembler.hasToolCalls());
  const std::vector<ToolCallBlock> calls = assembler.toolCalls();
  CHECK_EQ(calls.size(), size_t(1));
  CHECK_EQ(calls[0].id.value, std::string("c9"));
  CHECK_EQ(calls[0].name, std::string("grep"));
  CHECK_EQ(calls[0].arguments, std::string("{}{\"p\":1}"));
}

// dsh 载荷词汇: provider/model 在 source 内, tool/result 的 toolCallId 与
// source.callId 一致, 压缩检查点的 source 扩展键往返。
void testDshPayloadVocabulary() {
  TokenUsage usage;
  usage.inputTokens = 7;
  usage.outputTokens = 3;

  // assistant/message: wire 上 usage 在 data 内, provider/model 在 source 内。
  SessionEvent assistantEvent;
  assistantEvent.type = EventType::AssistantMessageEvent;
  assistantEvent.seq = 9;
  assistantEvent.timeMs = 100;
  AssistantMessage message = assistantMessage("答案");
  assistantEvent.data = AssistantMessageData{1, 2, message, usage};
  assistantEvent.surfaceOp = SurfaceAppend{};
  assistantEvent.sourceEventSeqs = std::vector<size_t>{5, 6, 7};
  const Json a = parserJson(encodeEvent(assistantEvent).c_str());
  const Json::JsonObject& envelope = a.get<Json::JsonObject>();
  CHECK_EQ(envelope.size(), size_t(6));
  CHECK(a["data"]["message"]["source"]["kind"].get<std::string>() == "model");
  CHECK(a["data"]["message"]["source"]["provider"].get<std::string>() == "test");
  CHECK(a["data"]["message"]["source"]["model"].get<std::string>() == "test-model");
  CHECK(a["data"]["message"]["role"].get<std::string>() == "assistant");
  CHECK(a["data"]["usage"]["inputTokens"].bInt()
        && a["data"]["usage"]["inputTokens"].get<int64_t>() == 7);
  CHECK(a["sourceEventSeqs"].bArray() && a["sourceEventSeqs"].size() == 3);
  CHECK(a["sourceEventSeqs"].at(0).get<int64_t>() == 5);
  CHECK(a["surfaceOp"].get<std::string>() == "append");

  // tool/result: toolCallId === source.callId (dsh 硬校验) 且 role 为 user。
  SessionEvent toolEvent;
  toolEvent.type = EventType::ToolResult;
  toolEvent.seq = 10;
  toolEvent.timeMs = 101;
  toolEvent.data = toolResult(1, 2, CallId("c1"), "输出", false);
  toolEvent.surfaceOp = SurfaceAppend{};
  const Json t = parserJson(encodeEvent(toolEvent).c_str());
  CHECK(t["data"]["message"]["role"].get<std::string>() == "user");
  CHECK(t["data"]["message"]["source"]["kind"].get<std::string>() == "tool");
  CHECK_EQ(t["data"]["message"]["content"].at(0)["toolCallId"].get<std::string>(),
           t["data"]["message"]["source"]["callId"].get<std::string>());
  CHECK(t["data"]["message"]["content"].size() == 1);
  CHECK(t["data"]["message"]["content"].at(0)["type"].get<std::string>()
        == "tool-result");

  // 压缩检查点 source 的扩展键往返。
  UserMessage summary = userMessage("sum", "纪要", compactCheckpointSource("cx-9"));
  SessionEvent compactEvent;
  compactEvent.type = EventType::UserMessageEvent;
  compactEvent.seq = 11;
  compactEvent.timeMs = 102;
  compactEvent.data = UserMessageData{summary};
  compactEvent.surfaceOp = SurfaceAppend{};
  const DecodedEvent decodedCompact = decodeEvent(encodeEvent(compactEvent));
  const auto& back = std::get<UserMessageData>(decodedCompact.event.data).message;
  CHECK(back.source.kind == MessageSourceKind::Plugin);
  CHECK(back.source.plugin.value_or("") == "compact");
  CHECK(back.source.compactionId.value_or("") == "cx-9");
}

// 打包存储行 (dsh chunk-rows.ts): 展开、时间累计、attach 对齐、坏行拒读。
void testPackedRows() {
  const fs::path root = freshDir("avox_session_test_packed");
  const std::string path = (root / "packed.jsonl").string();
  writeAllLines(path, {
      R"jsonl({"type":"session","version":0,"id":"packed","createdAt":1,"cwd":"D:\\t","delegationDepth":0})jsonl",
      R"jsonl({"type":"turn/start","seq":0,"time":10,"data":{"turn":1}})jsonl",
      R"jsonl({"type":"text-chunks","seq0":1,"time0":100,"data":{"turn":1,"step":1,"index":0,"dt":[2,3],"texts":["a","bb","ccc"]}})jsonl",
      R"jsonl({"type":"tool-call-chunks","seq0":4,"time0":200,"data":{"turn":1,"step":1,"index":2,"id":"c1","name":"grep","dt":[5],"args":["{\"p\":","1}"]}})jsonl",
      R"jsonl({"type":"step/end","seq":6,"time":300,"data":{"turn":1,"step":1}})jsonl",
      R"jsonl({"type":"turn/end","seq":7,"time":301,"data":{"turn":1,"reason":{"kind":"completed"}}})jsonl",
  });

  LoadedSession loaded = loadSession(path);
  CHECK_EQ(loaded.events.size(), size_t(8));
  CHECK(!loaded.repairedInterruptedTail);
  // text-chunks 展开: seq 1..3, time = time0 + 前缀和。
  CHECK(loaded.events[1].type == EventType::AssistantChunk);
  CHECK(loaded.events[3].type == EventType::AssistantChunk);
  const auto& first = std::get<AssistantChunkData>(loaded.events[1].data);
  const auto* delta1 = std::get_if<StreamTextDelta>(&first.chunk);
  CHECK(delta1 != nullptr && delta1->index == 0 && delta1->text == "a");
  CHECK_EQ(loaded.events[2].timeMs, int64_t(102));
  const auto* delta3 = std::get_if<StreamTextDelta>(
      &std::get<AssistantChunkData>(loaded.events[3].data).chunk);
  CHECK(delta3 != nullptr && delta3->text == "ccc");
  CHECK_EQ(loaded.events[3].timeMs, int64_t(105));
  // tool-call-chunks 展开: name 每成员都带, argumentsDelta 逐片。
  const auto& callFirst = std::get<AssistantChunkData>(loaded.events[4].data);
  const auto* callDelta = std::get_if<StreamToolCallDelta>(&callFirst.chunk);
  CHECK(callDelta != nullptr && callDelta->id.value == "c1"
        && callDelta->name.value_or("") == "grep" && callDelta->argumentsDelta == "{\"p\":");
  const auto* callSecond = std::get_if<StreamToolCallDelta>(
      &std::get<AssistantChunkData>(loaded.events[5].data).chunk);
  CHECK(callSecond != nullptr && callSecond->argumentsDelta == "1}"
        && callSecond->name.value_or("") == "grep");
  CHECK_EQ(loaded.events[5].timeMs, int64_t(205));

  // attach 对齐按成员数计: 构造补 end-seed 后只写 1 行, 重复 attach 不再长。
  {
    Session session(SessionId("packed"), std::move(loaded.events),
                    std::move(loaded.header));
    CHECK_EQ(session.events().size(), size_t(9));
    SessionWriter writer;
    CHECK(writer.attach(session, path));
    writer.detach();
  }
  {
    LoadedSession reloaded = loadSession(path);
    CHECK_EQ(reloaded.events.size(), size_t(9));
    Session session(SessionId("packed"), std::move(reloaded.events),
                    std::move(reloaded.header));
    SessionWriter writer;
    CHECK(writer.attach(session, path));
    writer.detach();
  }
  CHECK_EQ(loadSession(path).events.size(), size_t(9));

  // 坏行 (dt 配不平) 是损坏的存储, 必须响亮拒读。
  const std::string bad = (root / "bad.jsonl").string();
  writeAllLines(bad, {
      R"jsonl({"type":"session","version":0,"id":"bad","createdAt":1,"delegationDepth":0})jsonl",
      R"jsonl({"type":"turn/start","seq":0,"time":10,"data":{"turn":1}})jsonl",
      R"jsonl({"type":"text-chunks","seq0":1,"time0":100,"data":{"turn":1,"step":1,"index":0,"dt":[1],"texts":["a","bb","ccc"]}})jsonl",
  });
  CHECK_THROWS(loadSession(bad));

  // 信封多键同样拒 (exact-key)。
  const std::string extra = (root / "extra.jsonl").string();
  writeAllLines(extra, {
      R"jsonl({"type":"session","version":0,"id":"e","createdAt":1,"delegationDepth":0})jsonl",
      R"jsonl({"type":"text-chunks","seq0":0,"time0":1,"data":{"turn":1,"step":1,"index":0,"dt":[],"texts":["a"]},"ignorable":true})jsonl",
  });
  CHECK_THROWS(loadSession(extra));

  fs::remove_all(root);
}

// dsh 独有事件读进墓碑: seq 槽位保住, surface 不受影响, 原始行可逐字节回放;
// 名单外的未知类型且无 ignorable → 拒读。
void testTombstoneLoad() {
  const fs::path root = freshDir("avox_session_test_tomb");
  const std::string path = (root / "tomb.jsonl").string();
  // todo/write 已是 avox 原生事件 (data.todos 形状, 编解码往返已覆盖), 不再进墓碑;
  // 这里用白名单里的 llm/retry 当逐字节回放样本。
  const std::string tombLine =
      R"jsonl({"type":"llm/retry","seq":0,"time":1,"data":{"attempt":1,"reason":"RATE_LIMITED"}})jsonl";
  writeAllLines(path, {
      R"jsonl({"type":"session","version":0,"id":"tomb","createdAt":1,"cwd":"D:\\t","delegationDepth":0})jsonl",
      tombLine,
      R"jsonl({"type":"hook/invoked","seq":1,"time":2,"data":{"hook":"fmt","matcher":"**"}})jsonl",
      R"jsonl({"type":"plan/mode","seq":2,"time":3,"data":{"mode":"plan"}})jsonl",
      R"jsonl({"type":"turn/start","seq":3,"time":4,"data":{"turn":1}})jsonl",
      R"jsonl({"type":"user/message","seq":4,"time":5,"data":{"id":"m1","role":"user","content":[{"type":"text","text":"问题"}],"source":{"kind":"user"}},"surfaceOp":"append"})jsonl",
      R"jsonl({"type":"turn/end","seq":5,"time":6,"data":{"turn":1,"reason":{"kind":"completed"}}})jsonl",
  });

  LoadedSession loaded = loadSession(path);
  CHECK_EQ(loaded.events.size(), size_t(6));
  CHECK_EQ(loaded.skippedIgnorable, size_t(0));
  // 墓碑保住 seq 槽位 (seq == 下标契约) 与原始行。
  CHECK(loaded.events[0].type == EventType::Opaque);
  CHECK_EQ(std::get<OpaqueEventData>(loaded.events[0].data).typeName,
           std::string("llm/retry"));
  CHECK_EQ(encodeEvent(loaded.events[0]), tombLine);
  CHECK(loaded.events[1].type == EventType::Opaque);
  CHECK(loaded.events[2].type == EventType::Opaque);

  Session session(SessionId("tomb"), std::move(loaded.events),
                  std::move(loaded.header));
  // surface 只有那条 user 消息。
  CHECK_EQ(session.deriveMessages().size(), size_t(1));

  // 名单外且无 ignorable: 拒绝重建。
  const std::string alien = (root / "alien.jsonl").string();
  writeAllLines(alien, {
      R"jsonl({"type":"session","version":0,"id":"a","createdAt":1,"delegationDepth":0})jsonl",
      R"jsonl({"type":"alien/thing","seq":0,"time":1,"data":{}})jsonl",
  });
  CHECK_THROWS(loadSession(alien));

  fs::remove_all(root);
}

// zstd 压缩档拒读 (avox 全明文)。
void testZstdRefused() {
  const fs::path root = freshDir("avox_session_test_zstd");
  const std::string path = (root / "z.jsonl").string();
  const char magic[] = {0x28, static_cast<char>(0xB5), 0x2F, static_cast<char>(0xFD),
                        'j', 'u', 'n', 'k'};
  {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(magic, sizeof(magic));
  }
  CHECK_THROWS(loadSession(path));
  // attach 同样拒绝, 且失败原因可查。
  SessionHeader header;
  header.version = SESSION_FORMAT_VERSION;
  header.id = SessionId("z");
  Session session(SessionId("z"), std::vector<SessionEvent>{}, header);
  SessionWriter writer;
  CHECK(!writer.attach(session, path));
  CHECK(!writer.lastError().empty());
  fs::remove_all(root);
}

// ---- 附件桥测试用的 1x1 PNG (运行期构造, CRC/adler 全部真算, 不背字节常量) ----

uint32_t crc32Of(const std::vector<uint8_t>& bytes) {
  uint32_t crc = 0xFFFFFFFFu;
  for (uint8_t byte : bytes) {
    crc ^= byte;
    for (int k = 0; k < 8; ++k) {
      crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
  }
  return crc ^ 0xFFFFFFFFu;
}

uint32_t adler32Of(const std::vector<uint8_t>& bytes) {
  uint32_t a = 1;
  uint32_t b = 0;
  for (uint8_t byte : bytes) {
    a = (a + byte) % 65521u;
    b = (b + a) % 65521u;
  }
  return (b << 16) | a;
}

void appendBe32(std::vector<uint8_t>& out, uint32_t value) {
  out.push_back(static_cast<uint8_t>(value >> 24));
  out.push_back(static_cast<uint8_t>(value >> 16));
  out.push_back(static_cast<uint8_t>(value >> 8));
  out.push_back(static_cast<uint8_t>(value));
}

// 1x1 不透明红 PNG (RGBA, colorType 6)。zlib 用 stored deflate 块 —— 不需要压缩器,
// 解码器照收。
std::vector<uint8_t> buildPng1x1() {
  std::vector<uint8_t> png = {0x89, 'P',  'N',  'G',  '\r', '\n', 0x1A, '\n'};
  const auto chunk = [&png](const char* name, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> body(name, name + std::strlen(name));
    body.insert(body.end(), data.begin(), data.end());
    appendBe32(png, static_cast<uint32_t>(data.size()));
    png.insert(png.end(), body.begin(), body.end());
    appendBe32(png, crc32Of(body));
  };
  // IHDR: w=1 h=1 bitDepth=8 colorType=6 其余 0。
  chunk("IHDR", {0, 0, 0, 1, 0, 0, 0, 1, 8, 6, 0, 0, 0});
  // 扫描线: 滤波字节 0 + RGBA。
  const std::vector<uint8_t> scanline = {0x00, 0xFF, 0x00, 0x00, 0xFF};
  std::vector<uint8_t> zlib = {0x78, 0x01, 0x01, 0x05, 0x00, 0xFA, 0xFF};
  zlib.insert(zlib.end(), scanline.begin(), scanline.end());
  appendBe32(zlib, adler32Of(scanline));
  chunk("IDAT", zlib);
  chunk("IEND", {});
  return png;
}

// 用 avox 的真实编码器造一张 width x height 的纯色 PNG (小体积大尺寸 —— 手搓
// stored-deflate 做不到: 不压缩的扫描线对 3000x3000 就有 45MB, 会先撞字节限额)。
std::vector<uint8_t> encodeSolidPng(const fs::path& file, int width, int height) {
  IImageBuffer* buffer = createImageBuffer();
  if (buffer == nullptr) throw std::runtime_error("createImageBuffer 失败");
  ImageFormat format;
  format.width = width;
  format.height = height;
  format.imageType = ImageType::rgb8;
  buffer->setImageFormat(format);
  std::memset(buffer->getPointer(), 0x40,
              static_cast<size_t>(buffer->getBufferSize()));
  const bool saved = saveImagePath(file.string().c_str(), buffer);
  delete buffer;
  if (!saved) throw std::runtime_error("saveImagePath 失败: " + file.string());
  std::ifstream in(file, std::ios::binary);
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
  if (bytes.empty()) throw std::runtime_error("编码出的 PNG 为空: " + file.string());
  return bytes;
}

// 附件桥: 与 dsh 共用同一物理布局, 发布/装载/摘要校验/媒体类型核对。
void testAttachmentBridge() {
  const fs::path root = freshDir("avox_session_test_attach");
  const std::string storeRoot = (root / "attachments" / "v1").string();
  DshAttachmentStore store(storeRoot);

  const std::vector<uint8_t> png = buildPng1x1();
  const ImageAttachmentRef ref =
      store.publish(png.data(), png.size(), "image/png", "C:\\tmp\\红点.png");

  // 引用 id 是内容的 sha256 (小写 hex)。
  Sha256 sha;
  sha.update(png.data(), png.size());
  uint8_t digest[32];
  sha.finalize(digest);
  const std::string hex = hexEncode(std::vector<uint8_t>(digest, digest + 32));
  CHECK_EQ(ref.attachmentId, "sha256:" + hex);
  CHECK_EQ(ref.mediaType, std::string("image/png"));
  CHECK_EQ(ref.bytes, static_cast<int64_t>(png.size()));
  // 宽高从字节解出 (1x1)。
  CHECK_EQ(ref.width, int64_t(1));
  CHECK_EQ(ref.height, int64_t(1));
  // 展示名剥掉了本地路径。
  CHECK_EQ(ref.name.value_or(""), std::string("红点.png"));
  // 对象落在内容寻址布局: objects/<前2 hex>/<hex>。
  CHECK(fs::exists(fs::path(storeRoot) / "objects" / hex.substr(0, 2) / hex));

  // 装载往返逐字节一致。
  const std::vector<uint8_t> back = store.load(ref);
  CHECK(back == png);

  // 声明的媒体类型与字节不符 → 拒绝。
  CHECK_THROWS(store.publish(png.data(), png.size(), "image/jpeg", std::nullopt));
  // 引用被篡改 (字节数不符) → 拒绝。
  ImageAttachmentRef tampered = ref;
  tampered.bytes += 1;
  CHECK_THROWS(store.load(tampered));

  // —— 准入限额 (dsh #2629/#2623): 已入仓的图随历史搭每一次请求, 拒在入仓线 ——
  // 超字节: 合法 PNG 尾部垫到 3.5MB 以上 —— 字节检查先于任何解码 (dsh saveImageFile 同序)。
  std::vector<uint8_t> padded = png;
  padded.resize(static_cast<size_t>(kImageMaxBytes) + 1, 0);
  CHECK_THROWS_WITH(store.publish(padded.data(), padded.size(), "image/png",
                                  std::nullopt),
                    "IMAGE_TOO_LARGE");
  // 超像素: 6500x6500 = 42.25M > 40M (两轴均过解码, 限额在解码成功之后)。
  const std::vector<uint8_t> manyPixels =
      encodeSolidPng(root / "many_pixels.png", 6500, 6500);
  CHECK_THROWS_WITH(store.publish(manyPixels.data(), manyPixels.size(),
                                  "image/png", std::nullopt),
                    "IMAGE_TOO_MANY_PIXELS");
  // 单边超限: 2001x2001 = 4M 像素 (不触像素限额), 边 2001 > 2000。
  const std::vector<uint8_t> tallSide =
      encodeSolidPng(root / "tall_side.png", 2001, 2001);
  CHECK_THROWS_WITH(store.publish(tallSide.data(), tallSide.size(), "image/png",
                                  std::nullopt),
                    "IMAGE_DIMENSION_TOO_LARGE");
  // 边界内 (2000x2000 = 4M 像素, 单边恰在限上) 正常入仓。
  const std::vector<uint8_t> atLimit =
      encodeSolidPng(root / "at_limit.png", 2000, 2000);
  const ImageAttachmentRef atLimitRef =
      store.publish(atLimit.data(), atLimit.size(), "image/png", std::nullopt);
  CHECK_EQ(atLimitRef.width, int64_t(2000));
  CHECK_EQ(atLimitRef.height, int64_t(2000));

  fs::remove_all(root);
}

// ChatProvider wire 形状 (dsh serialize.ts 逐条对照): tool 图片卸载到后续 user 消息、
// 占位文本、纯文本紧凑形态、CoT 回传、stream_options。
void testChatProviderWire() {
  ProviderRequest req;
  req.model = "test-model";
  req.systemPrompt = "sys";
  req.temperature = 0.5f;
  req.maxTokens = 128;

  // user 纯文本 (两个 text part) → join 成单个 string。
  ProviderMessage user1;
  user1.role = ProviderRole::User;
  user1.parts.push_back(ProviderPart{PartType::Text, "你好 "});
  user1.parts.push_back(ProviderPart{PartType::Text, "世界"});
  req.messages.push_back(user1);

  // assistant 带 tool_calls + CoT 回传。
  ProviderMessage assistant;
  assistant.role = ProviderRole::Assistant;
  assistant.assistantToolCallsWire =
      R"([{"id":"call_1","type":"function","function":{"name":"read_image","arguments":"{}"}}])";
  assistant.assistantReasoning = "想想";
  req.messages.push_back(assistant);

  // tool 结果: 文本 + 图 → 图外提。
  ProviderMessage tool1;
  tool1.role = ProviderRole::Tool;
  tool1.toolCallId = "call_1";
  tool1.parts.push_back(ProviderPart{PartType::Text, "<path>x.png</path>"});
  tool1.parts.push_back(
      ProviderPart{PartType::ImageUrl, "data:image/png;base64,AAAA"});
  req.messages.push_back(tool1);

  // 第二个 tool 结果: 只有图 → 占位 "(see attached image)"。
  ProviderMessage tool2;
  tool2.role = ProviderRole::Tool;
  tool2.toolCallId = "call_2";
  tool2.parts.push_back(
      ProviderPart{PartType::ImageUrl, "data:image/png;base64,BBBB"});
  req.messages.push_back(tool2);

  // 空 tool 结果 → "(no output)"。
  ProviderMessage tool3;
  tool3.role = ProviderRole::Tool;
  tool3.toolCallId = "call_3";
  req.messages.push_back(tool3);

  // 后续 user (自带图): flush 的工具图消息应排在它前面。
  ProviderMessage user2;
  user2.role = ProviderRole::User;
  user2.parts.push_back(ProviderPart{PartType::Text, "看第二张"});
  user2.parts.push_back(
      ProviderPart{PartType::ImageUrl, "data:image/png;base64,CCCC"});
  req.messages.push_back(user2);

  ChatProvider provider;
  const std::string payload = provider.buildPayload(req);
  Json wire = parserJson(payload.c_str());
  Json& msgs = wire["messages"];
  CHECK(msgs.bArray());
  // system, user1, assistant, tool1, tool2, tool3, flush-user(2 图), user2。
  CHECK_EQ(msgs.size(), size_t(8));

  CHECK_EQ(msgs[0]["role"].get<std::string>(), std::string("system"));
  CHECK_EQ(msgs[0]["content"].get<std::string>(), std::string("sys"));
  CHECK_EQ(msgs[1]["role"].get<std::string>(), std::string("user"));
  CHECK_EQ(msgs[1]["content"].get<std::string>(), std::string("你好 世界"));
  // assistant: content "" (绝不能 null) + reasoning_content + tool_calls。
  CHECK_EQ(msgs[2]["role"].get<std::string>(), std::string("assistant"));
  CHECK_EQ(msgs[2]["content"].get<std::string>(), std::string(""));
  CHECK_EQ(msgs[2]["reasoning_content"].get<std::string>(),
           std::string("想想"));
  CHECK_EQ(msgs[2]["tool_calls"].size(), size_t(1));
  // tool1: 文本留下, 图不在此消息。
  CHECK_EQ(msgs[3]["role"].get<std::string>(), std::string("tool"));
  CHECK_EQ(msgs[3]["tool_call_id"].get<std::string>(), std::string("call_1"));
  CHECK_EQ(msgs[3]["content"].get<std::string>(),
           std::string("<path>x.png</path>"));
  // tool2: 只有图 → 占位文本。
  CHECK_EQ(msgs[4]["content"].get<std::string>(),
           std::string("(see attached image)"));
  // tool3: 空 → "(no output)"。
  CHECK_EQ(msgs[5]["content"].get<std::string>(), std::string("(no output)"));
  // flush 的 user 消息: 前缀文本 + 两个图 part (按原序)。
  CHECK_EQ(msgs[6]["role"].get<std::string>(), std::string("user"));
  CHECK(msgs[6]["content"].bArray());
  CHECK_EQ(msgs[6]["content"].size(), size_t(3));
  CHECK_EQ(msgs[6]["content"][0]["type"].get<std::string>(),
           std::string("text"));
  CHECK_EQ(msgs[6]["content"][0]["text"].get<std::string>(),
           std::string("Attached image(s) from tool result:"));
  CHECK_EQ(msgs[6]["content"][1]["image_url"]["url"].get<std::string>(),
           std::string("data:image/png;base64,AAAA"));
  CHECK_EQ(msgs[6]["content"][2]["image_url"]["url"].get<std::string>(),
           std::string("data:image/png;base64,BBBB"));
  // user2: 文本 + 图 → 数组形态, 序保持。
  CHECK_EQ(msgs[7]["content"].size(), size_t(2));
  CHECK_EQ(msgs[7]["content"][0]["text"].get<std::string>(),
           std::string("看第二张"));
  CHECK_EQ(msgs[7]["content"][1]["image_url"]["url"].get<std::string>(),
           std::string("data:image/png;base64,CCCC"));
  // stream_options: DeepSeek 官方流式回 usage 的开关。
  CHECK_EQ(wire["stream_options"]["include_usage"].get<bool>(), true);
}

// 模型路由重试臂 (dsh 0ca0f3d0b8 统一 5 次重试默认): 默认 6 次尝试 = 首次 + 5 次重试;
// 认领码对齐 dsh DEFAULT_RETRYABLE_CODES 的 avox 映射 (含 CONNECTION_FAILED); 认证失败
// 不认领; Retry-After ≤ max 采原值重试、超上限放弃; 等待可被 abort 唤醒。
// 退避全部调到 1ms 档 (或预先 abort), 不真等指数延迟。
void testModelRouteRetry() {
  AgentExtensionPoints points;
  const auto giveUp = []() -> RequestErrorAction { return std::nullopt; };
  // 默认次数: 5 次重试后终结; 非认领码原样委派。
  {
    ModelRoutePolicyConfig config;
    config.backoffInitialMs = 1;
    const Disposer dispose = installModelRoutePolicy(points, nullptr, config);
    AbortController controller;
    RequestErrorPayload payload;
    payload.signal = controller.signal();
    payload.failure = LlmFailure{"限流", "RATE_LIMITED"};
    for (int i = 0; i < 5; ++i) {
      CHECK(points.requestError.run(payload, nullptr, giveUp).has_value());
    }
    CHECK(!points.requestError.run(payload, nullptr, giveUp).has_value());
    payload.failure = LlmFailure{"key 无效", "AUTH_FAILED"};
    CHECK(!points.requestError.run(payload, nullptr, giveUp).has_value());
    dispose();
  }
  // Retry-After 语义 (dsh normal 模式): ≤ max 采纳原值照常重试; > max 等不起就放弃。
  {
    ModelRoutePolicyConfig config;
    config.backoffInitialMs = 1;
    config.backoffMaxMs = 100;
    const Disposer dispose = installModelRoutePolicy(points, nullptr, config);
    AbortController controller;
    RequestErrorPayload payload;
    payload.signal = controller.signal();
    payload.failure = LlmFailure{"限流", "RATE_LIMITED"};
    payload.failure.providerRetryAfterMs = 50;
    CHECK(points.requestError.run(payload, nullptr, giveUp).has_value());
    payload.failure.providerRetryAfterMs = 5000;
    CHECK(!points.requestError.run(payload, nullptr, giveUp).has_value());
    dispose();
  }
  // abort 唤醒: 已取消的 signal 让退避等待立即返回 nullopt (不重试)。退避调到 10s,
  // 若等待不可唤醒, 本段会挂 10 秒才失败。
  {
    ModelRoutePolicyConfig config;
    config.backoffInitialMs = 10000;
    const Disposer dispose = installModelRoutePolicy(points, nullptr, config);
    AbortController controller;
    controller.abort(AgentCancelCause{CancelByUser{}});
    RequestErrorPayload payload;
    payload.signal = controller.signal();
    payload.failure = LlmFailure{"断连", "CONNECTION_FAILED"};
    CHECK(!points.requestError.run(payload, nullptr, giveUp).has_value());
    dispose();
  }
}

// dsh 真实日志装载: examples/acp-agent snapshots/text-turn 实体化占位符
// ({{cwd}}/{{system}}/{{tools}}) 后逐字节内嵌。这是「avox 读得懂 dsh 产的日志」
// 的端到端证据 —— 含打包行、墓碑、inbox splice、插件 source 扩展键。
void testDshGoldenLogLoad() {
  const fs::path root = freshDir("avox_session_test_golden");
  const std::string path = (root / "golden.jsonl").string();
  const std::string titleLine =
      R"jsonl({"type":"session/title","seq":6,"time":1785730415287,"data":{"title":"Reply with exactly the word:","messageSeqs":[4],"source":{"kind":"fallback"}}})jsonl";
  writeAllLines(path, {
      R"jsonl({"type":"session","version":0,"id":"539aa64c-7f37-40ff-abd8-ed45b717be1b","createdAt":1783600629539,"cwd":"D:\\Work\\github\\avox","delegationDepth":0})jsonl",
      R"jsonl({"type":"agent/inbox/spliced","seq":0,"time":1785498761270,"data":{"target":"next-turn","start":0,"inserted":[{"content":[{"type":"text","text":"Reply with exactly the word: PONG. Do not use any tools."}],"source":{"kind":"user"},"role":"user","id":"3e25dc34-48e0-4738-8401-1a8d181d37e5"}]}})jsonl",
      R"jsonl({"type":"turn/start","seq":1,"time":1785821359466,"data":{"turn":1}})jsonl",
      R"jsonl({"type":"agent/inbox/spliced","seq":2,"time":1785821359466,"data":{"target":"next-turn","start":0,"removedCount":1,"inserted":[]}})jsonl",
      R"jsonl({"type":"step/start","seq":3,"time":1783600629542,"data":{"turn":1,"step":1}})jsonl",
      R"jsonl({"type":"user/message","seq":4,"time":1785498761313,"data":{"content":[{"type":"text","text":"Reply with exactly the word: PONG. Do not use any tools."}],"source":{"kind":"user"},"role":"user","id":"3e25dc34-48e0-4738-8401-1a8d181d37e5"},"surfaceOp":"append"})jsonl",
      R"jsonl({"type":"user/message","seq":5,"time":1785730415287,"data":{"content":[{"type":"text","text":"Current runtime context. This snapshot supersedes earlier runtime-context snapshots."}],"source":{"kind":"plugin","plugin":"@deepseek-ai/dsh-system-prompt","form":"snapshot","sections":[{"name":"sandbox:policy","text":"..."}]},"role":"user","id":"4b8d9730-0b7b-4e14-8a30-3d852f808f0e"},"surfaceOp":"append"})jsonl",
      titleLine,
      R"jsonl({"type":"request/header","seq":7,"time":1785498761318,"data":{"header":{"config":{"provider":"deepseek-official","model":"deepseek-v4-flash"},"system":"You are a test harness.","tools":[]},"reason":"initial"}})jsonl",
      R"jsonl({"type":"request/context","seq":8,"time":1785730415288,"data":{"provider":"deepseek-official","model":"deepseek-v4-flash"}})jsonl",
      R"jsonl({"type":"assistant/chunk","seq":9,"time":1783600630822,"data":{"turn":1,"step":1,"chunk":{"type":"block-start","index":0,"blockType":"reasoning"}}})jsonl",
      R"jsonl({"type":"reasoning-chunks","seq0":10,"time0":1783600630852,"data":{"turn":1,"step":1,"index":0,"dt":[0,0,0,33,1,40,0,0,0,0,0,18,0,36,0,0,0,0,0],"texts":["The"," user"," wants"," me"," to"," reply"," with"," exactly"," the"," word"," \"","P","ONG","\""," and"," not"," use"," any"," tools","."]}})jsonl",
      R"jsonl({"type":"assistant/chunk","seq":30,"time":1783600631006,"data":{"turn":1,"step":1,"chunk":{"type":"block-start","index":1,"blockType":"text"}}})jsonl",
      R"jsonl({"type":"assistant/chunk","seq":31,"time":1783600631008,"data":{"turn":1,"step":1,"chunk":{"type":"text-delta","index":1,"text":"P"}}})jsonl",
      R"jsonl({"type":"assistant/chunk","seq":32,"time":1783600631009,"data":{"turn":1,"step":1,"chunk":{"type":"text-delta","index":1,"text":"ONG"}}})jsonl",
      R"jsonl({"type":"assistant/chunk","seq":33,"time":1783600631009,"data":{"turn":1,"step":1,"chunk":{"type":"block-end","index":0,"block":{"type":"reasoning","text":"The user wants me to reply with exactly the word \"PONG\" and not use any tools."}}}})jsonl",
      R"jsonl({"type":"assistant/chunk","seq":34,"time":1783600631009,"data":{"turn":1,"step":1,"chunk":{"type":"block-end","index":1,"block":{"type":"text","text":"PONG"}}}})jsonl",
      R"jsonl({"type":"assistant/chunk","seq":35,"time":1785498761338,"data":{"turn":1,"step":1,"chunk":{"type":"usage","usage":{"inputTokens":3091,"outputTokens":23,"cacheReadTokens":0,"reasoningTokens":20}}}})jsonl",
      R"jsonl({"type":"assistant/chunk","seq":36,"time":1785730415297,"data":{"turn":1,"step":1,"chunk":{"type":"finish","reason":{"kind":"stop"}}}})jsonl",
      R"jsonl({"type":"assistant/message","seq":37,"time":1785730415298,"data":{"turn":1,"step":1,"message":{"role":"assistant","content":[{"type":"reasoning","text":"The user wants me to reply with exactly the word \"PONG\" and not use any tools."},{"type":"text","text":"PONG"}],"source":{"kind":"model","provider":"deepseek-official","model":"deepseek-v4-flash"},"id":"3b028c0c-080e-4de0-8339-9aef7fa4769f"},"usage":{"inputTokens":3091,"outputTokens":23,"cacheReadTokens":0,"reasoningTokens":20}},"sourceEventSeqs":[9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36],"surfaceOp":"append"})jsonl",
      R"jsonl({"type":"step/end","seq":38,"time":1785730415298,"data":{"turn":1,"step":1}})jsonl",
      R"jsonl({"type":"turn/end","seq":39,"time":1785730415298,"data":{"turn":1,"reason":{"kind":"completed"}}})jsonl",
  });

  LoadedSession loaded = loadSession(path);
  CHECK_EQ(loaded.events.size(), size_t(40));
  CHECK_EQ(loaded.skippedIgnorable, size_t(0));
  CHECK(!loaded.repairedInterruptedTail);
  CHECK_EQ(loaded.header.id.value,
           std::string("539aa64c-7f37-40ff-abd8-ed45b717be1b"));
  CHECK_EQ(loaded.header.cwd.value_or(""), std::string("D:\\Work\\github\\avox"));
  CHECK_EQ(loaded.header.delegationDepth.value_or(-1), 0);

  // 打包行展开出的分片 (seq 10..29)。
  CHECK(loaded.events[10].type == EventType::AssistantChunk);
  const auto& firstDelta = std::get<AssistantChunkData>(loaded.events[10].data);
  const auto* reasoning0 = std::get_if<StreamReasoningDelta>(&firstDelta.chunk);
  CHECK(reasoning0 != nullptr && reasoning0->text == "The");
  CHECK(loaded.events[29].type == EventType::AssistantChunk);
  const auto& lastDelta = std::get<AssistantChunkData>(loaded.events[29].data);
  const auto* reasoningLast = std::get_if<StreamReasoningDelta>(&lastDelta.chunk);
  CHECK(reasoningLast != nullptr && reasoningLast->text == ".");
  // time = time0 + Σdt = 1783600630852 + 128。
  CHECK_EQ(loaded.events[29].timeMs, int64_t(1783600630980));
  // 手写分片行照常解码。
  const auto& textP = std::get<AssistantChunkData>(loaded.events[31].data);
  const auto* textDelta = std::get_if<StreamTextDelta>(&textP.chunk);
  CHECK(textDelta != nullptr && textDelta->text == "P");
  // session/title 进墓碑且原始行逐字节保留。
  CHECK(loaded.events[6].type == EventType::Opaque);
  CHECK_EQ(encodeEvent(loaded.events[6]), titleLine);

  // assistant 消息: usage 记账与 source 内路由。
  const auto& assistant = std::get<AssistantMessageData>(loaded.events[37].data);
  CHECK_EQ(assistant.usage->inputTokens, int64_t(3091));
  CHECK(assistant.message.source.provider.value_or("") == "deepseek-official");

  Session session(SessionId("539aa64c-7f37-40ff-abd8-ed45b717be1b"),
                  std::move(loaded.events), std::move(loaded.header));
  // 模型可见历史: 两条 user (含插件注入的 runtime context) + 一条 assistant。
  CHECK_EQ(session.deriveMessages().size(), size_t(3));
  const auto& reply = std::get<AssistantMessage>(session.deriveMessages()[2]);
  CHECK_EQ(reply.content.size(), size_t(2));
  const auto* replyText = std::get_if<TextBlock>(&reply.content[1]);
  CHECK(replyText != nullptr && replyText->text == "PONG");
  // 构造补 end-seed 后事件 41 条; attach 对齐写 1 行, 重复 attach 不再长。
  CHECK_EQ(session.events().size(), size_t(41));
  {
    SessionWriter writer;
    CHECK(writer.attach(session, path));
    writer.detach();
  }
  {
    LoadedSession reloaded = loadSession(path);
    CHECK_EQ(reloaded.events.size(), size_t(41));
    Session again(SessionId("539aa64c-7f37-40ff-abd8-ed45b717be1b"),
                  std::move(reloaded.events), std::move(reloaded.header));
    SessionWriter writer;
    CHECK(writer.attach(again, path));
    writer.detach();
  }
  CHECK_EQ(loadSession(path).events.size(), size_t(41));

  fs::remove_all(root);
}

}  // namespace

void run(const char* name, void (*test)()) {
  std::cout << "-- " << name << std::flush;
  try {
    test();
    std::cout << " ok" << std::endl;
  } catch (const std::exception& e) {
    std::cout << " 抛出异常: " << e.what() << std::endl;
    ++g_failures;
  } catch (...) {
    std::cout << " 抛出未知异常" << std::endl;
    ++g_failures;
  }
}

int main() {
  std::cout << "=== avox_agent/core 会话日志不变式自测 ===" << std::endl;

  run("seq 连续性", testSeqContiguity);
  run("投影跳过非 surface 事件", testProjectionSkipsNonSurface);
  run("空 assistant 消息不进历史", testEmptyAssistantMessageSkipped);
  run("压缩替换: 历史变短而事件不减",
      testCompactionReplaceShortensHistoryWithoutDeletingEvents);
  run("replace 校验", testReplaceValidation);
  run("tool/result 重写限制", testToolResultRewriteRestriction);
  run("编解码往返", testCodecRoundTrip);
  run("未识别事件类型处理", testUnknownEventTypeHandling);
  run("resume 保真 (工具历史可见)", testResumePreservesToolHistory);
  run("崩溃遗留 turn 补写 interrupted", testInterruptedTailRepair);
  run("header 折叠", testRequestHeaderFold);
  run("seed 校验", testSeedValidation);
  run("dsh 目录布局", testDshLayout);
  run("dsh 头行形状", testDshHeaderShape);
  run("delta 分片往返与组装", testDeltaChunkRoundTrip);
  run("dsh 载荷词汇", testDshPayloadVocabulary);
  run("打包存储行", testPackedRows);
  run("墓碑装载", testTombstoneLoad);
  run("zstd 压缩档拒读", testZstdRefused);
  run("附件桥", testAttachmentBridge);
  run("ChatProvider wire 形状", testChatProviderWire);
  run("模型路由重试臂 (dsh 5 次默认)", testModelRouteRetry);
  run("dsh 真实日志装载", testDshGoldenLogLoad);

  if (g_failures == 0) {
    std::cout << "全部通过" << std::endl;
    return 0;
  }
  std::cout << g_failures << " 项失败" << std::endl;
  return 1;
}
