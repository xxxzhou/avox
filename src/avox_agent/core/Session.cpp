#include "Session.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <utility>

#include "avox/module/Json.hpp"
#include "avox/module/LogHelper.hpp"

namespace avox {

namespace {

int64_t nowMillis() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch())
      .count();
}

// 校验一段字符串装的 JSON 确实是对象或数组。
//
// 为什么只认对象/数组: 本项目的 Json 在解析失败时返回 Null, 而合法的 "null" 也是 Null
// —— 两者无法区分。而这两个字段语义上本就是对象 (meta) 与数组 (tools schema), 所以
// 「必须是对象或数组」既是有效校验, 也避开了那个歧义。
void assertJsonObjectOrArray(const std::string& text, const char* field) {
  const Json parsed = parserJson(text.c_str());
  if (!parsed.bObject() && !parsed.bArray()) {
    throw std::runtime_error(std::string(field)
                             + " 必须是合法的 JSON 对象或数组");
  }
}

}  // namespace

// ===========================================================================
// 构造
// ===========================================================================

Session::Session(SessionId id, std::vector<SessionEvent> seed,
                 std::optional<SessionHeader> header) {
  const bool hasSeed = !seed.empty();

  eventLog.reserve(seed.size() + 1);
  for (size_t index = 0; index < seed.size(); ++index) {
    SessionEvent event = std::move(seed[index]);

    // 信封自洽: 类型标签必须与载荷的变体一致。JSONL 读回来的事件有可能两者不符
    // (手改过的 fixture、跨版本的编解码), 那种事件进了日志会让所有 std::get 走错分支。
    if (event.type != eventTypeOf(event.data)) {
      throw std::runtime_error("seed 第 " + std::to_string(index)
                               + " 条事件的类型标签与载荷不符");
    }
    // seq 必须从 0 连续 —— 全系统依赖 seq == 日志下标 这条契约。
    if (event.seq != index) {
      throw std::runtime_error(
          "seed 第 " + std::to_string(index) + " 条事件的 seq 是 "
          + std::to_string(event.seq) + " (应为 " + std::to_string(index)
          + "); seed 必须从 0 连续");
    }
    assertOpaqueJson(event.data);
    // 与 append 同一套 surface 不变式: 一个 replay / fork 不能构造出任何后端都存不下的
    // 活日志。校验在事件入库前完成, 于是失败不会让表面处于半变更状态。
    try {
      surfaceManager.validateNext(event, index);
    } catch (const std::exception& e) {
      throw std::runtime_error("seed 第 " + std::to_string(index)
                               + " 条事件非法: " + e.what());
    }
    eventLog.push_back(std::move(event));
  }

  firstLiveSeq = eventLog.size();

  if (header.has_value()) {
    this->header = std::move(*header);
    // id 是会话身份的单一副本, 传入的 header 必须与之一致。
    if (this->header.id.empty()) {
      this->header.id = id;
    } else if (this->header.id != id) {
      throw std::runtime_error("header 里的会话 id 与构造参数不一致");
    }
    if (this->header.version != SESSION_FORMAT_VERSION) {
      throw std::runtime_error(
          "会话格式版本 " + std::to_string(this->header.version)
          + " 不受支持 (当前 " + std::to_string(SESSION_FORMAT_VERSION)
          + "); 未发布期不提供迁移, 直接拒绝");
    }
  } else {
    this->header = SessionHeader{};
    this->header.version = SESSION_FORMAT_VERSION;
    this->header.id = std::move(id);
    this->header.createdAt = nowMillis();
  }

  // 在此追加, 于是后端捕获创建 seed 时标记已在 events() 里: 无需加载期回写。
  // 已带该标记的 seed 不重复补 —— 冷会话在首次触碰时才 resume, 反复打开一个未改动的
  // 会话不能让它的日志逐次变长。此刻还没有观察者, 所以这条也不会发布。
  if (hasSeed && eventLog.back().type != EventType::SessionEndSeed) {
    append(SessionEndSeedData{});
  }
}

// ===========================================================================
// 追加
// ===========================================================================

void Session::assertOpaqueJson(const EventData& data) {
  if (const auto* result = std::get_if<ToolResultData>(&data)) {
    if (result->meta.has_value()) {
      assertJsonObjectOrArray(*result->meta, "tool/result 的 meta");
    }
    return;
  }
  if (const auto* header = std::get_if<RequestHeaderData>(&data)) {
    if (header->header.toolsJson.has_value()) {
      assertJsonObjectOrArray(*header->header.toolsJson,
                              "request/header 的 toolsJson");
    }
  }
}

size_t Session::appendImpl(EventData data, std::optional<SurfaceIntent> intent) {
  // 重入检查放在最前: 一次在发布窗口内的重入会让 seq 与发布顺序交错, 观察者看到的日志
  // 就不再是「事件提交时的那份日志」。
  if (appending) {
    throw std::runtime_error("append 不得在另一条事件的发布窗口内重入");
  }
  // 墓碑只经装载白名单产生 (dsh 独有事件的忠实回放), 活跃会话没有它的语义 ——
  // 在这里挡住比让一条假 "todo/write" 流出好。
  if (std::holds_alternative<OpaqueEventData>(data)) {
    throw std::runtime_error("活跃会话不得产生 Opaque 墓碑事件");
  }
  assertOpaqueJson(data);

  SessionEvent event;
  event.type = eventTypeOf(data);
  event.seq = eventLog.size();
  event.timeMs = nowMillis();
  event.data = std::move(data);
  if (intent.has_value()) {
    event.surfaceOp = intent->surfaceOp;
    event.sourceEventSeqs = std::move(intent->sourceEventSeqs);
  }

  // 抛出则日志不变: 坏事件在追加现场失败。
  surfaceManager.validateNext(event, event.seq);

  // 先取观察者快照: 回调过程中有人注册或注销都不会让本次遍历失效, 也不会让新注册的
  // 观察者收到一条它注册之前就已提交的事件。
  const std::vector<SessionObserver*> snapshot = observers;

  eventLog.push_back(std::move(event));
  const SessionEvent& committed = eventLog.back();
  const size_t committedSeq = committed.seq;

  // 事件已进日志 = 已提交。此后无论观察者做什么都不能改变本次 append 的结果。
  appending = true;
  struct WindowGuard {
    bool& flag;
    ~WindowGuard() { flag = false; }
  } guard{appending};

  for (SessionObserver* ob : snapshot) {
    if (ob == nullptr) continue;
    try {
      ob->onSessionEvent(*this, committed);
    } catch (const std::exception& e) {
      LOGFLF(LogLevel::warn, "[session] 观察者处理 ",
             eventTypeName(committed.type), " 抛出异常: ", e.what());
    } catch (...) {
      LOGFLF(LogLevel::warn, "[session] 观察者处理 ",
             eventTypeName(committed.type), " 抛出未知异常");
    }
  }
  return committedSeq;
}

// ===========================================================================
// 投影
// ===========================================================================

const std::vector<Message>& Session::deriveMessages() {
  const size_t generation = surfaceManager.replaceGeneration();
  // 一次 replace 会重写表面中段, 增量续投影不再成立 —— 整体重建。
  if (generation != derivedGeneration) {
    derivedCache.clear();
    derivedNodes = 0;
    derivedGeneration = generation;
  }
  const std::vector<size_t>& nodes = surfaceManager.nodes();
  for (size_t i = derivedNodes; i < nodes.size(); ++i) {
    std::optional<Message> message = deriveEventMessage(eventLog[nodes[i]]);
    if (message.has_value()) derivedCache.push_back(std::move(*message));
  }
  derivedNodes = nodes.size();
  return derivedCache;
}

const EpochHeader* Session::requestHeader() {
  for (; headerFoldCount < eventLog.size(); ++headerFoldCount) {
    const SessionEvent& event = eventLog[headerFoldCount];
    if (event.type != EventType::RequestHeaderEvent) continue;
    headerFold = std::get<RequestHeaderData>(event.data).header;
  }
  return headerFold.has_value() ? &*headerFold : nullptr;
}

const RequestContext* Session::requestContext() {
  for (; contextFoldCount < eventLog.size(); ++contextFoldCount) {
    const SessionEvent& event = eventLog[contextFoldCount];
    if (event.type != EventType::RequestContextEvent) continue;
    contextFold = std::get<RequestContextData>(event.data).context;
  }
  return contextFold.has_value() ? &*contextFold : nullptr;
}

const std::vector<TodoItem>* Session::todos() {
  for (; todosFoldCount < eventLog.size(); ++todosFoldCount) {
    const SessionEvent& event = eventLog[todosFoldCount];
    if (event.type == EventType::TodoWrite) {
      todosFold = std::get<TodoWriteData>(event.data).todos;
    } else if (event.type == EventType::TurnStart) {
      // dsh 投影: turn 开始时清空 —— todo 只描述当前 turn 的工作面, 历史写入
      // 仍在日志里 (日志是唯一真相, 投影是它的瞬时视图)。
      todosFold.reset();
    }
  }
  return todosFold.has_value() ? &*todosFold : nullptr;
}

// ===========================================================================
// 观察者
// ===========================================================================

void Session::addObserver(SessionObserver* ob) {
  if (ob == nullptr) return;
  if (std::find(observers.begin(), observers.end(), ob) != observers.end()) {
    return;
  }
  observers.push_back(ob);
}

void Session::removeObserver(SessionObserver* ob) {
  observers.erase(std::remove(observers.begin(), observers.end(), ob),
                  observers.end());
}

}
