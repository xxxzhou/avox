#include "SessionPersistence.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <io.h>  // _chsize_s / _fileno
#else
#include <unistd.h>  // truncate
#endif

#include "SessionCodec.hpp"
#include "avox/module/Json.hpp"
#include "avox/module/LogHelper.hpp"

namespace avox {

namespace {

int64_t nowMillis() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch())
      .count();
}

// 读整个文件 (二进制)。会话日志几 MB 量级, 后续逐行解码本就要全部驻留。
std::optional<std::string> readAllBytes(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in.is_open()) return std::nullopt;
  return std::string((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
}

// zstd 帧魔数 28 B5 2F FD。
bool hasZstdMagic(const std::string& bytes) {
  return bytes.size() >= 4
      && static_cast<unsigned char>(bytes[0]) == 0x28
      && static_cast<unsigned char>(bytes[1]) == 0xB5
      && static_cast<unsigned char>(bytes[2]) == 0x2F
      && static_cast<unsigned char>(bytes[3]) == 0xFD;
}

// ---------- 打包存储行 (dsh chunk-rows.ts 的读侧) ----------
//
// text-chunks / reasoning-chunks / tool-call-chunks 不是事件类型, 是存储层把一串
// 连续同块 delta 分片压成一行的编码。读侧必须展开 (dsh 读是 layout-blind 的,
// 明文日志里也混着打包行); 形状非法响亮抛错 —— 静默跳过等于丢一整段回复。

bool isChunkRowTag(const std::string& tag) {
  return tag == "text-chunks" || tag == "reasoning-chunks"
      || tag == "tool-call-chunks";
}

// exact-key 校验 (dsh hasExactKeys: 键集必须恰好相等)。
bool hasExactKeys(const Json& value, std::initializer_list<const char*> keys) {
  const Json::JsonObject* object = value.get_ptr<const Json::JsonObject*>();
  if (object == nullptr) return false;
  if (object->size() != keys.size()) return false;
  for (const char* key : keys) {
    if (object->find(key) == object->end()) return false;
  }
  return true;
}

void malformedRow(const std::string& tag, const std::string& why) {
  throw std::runtime_error("malformed " + tag + " storage row: " + why);
}

// 数值取值: dsh 只要求 number; Json 把整数与浮点分开存, 整数值两种都认。
bool numberValue(const Json& value, int64_t& out) {
  if (value.bInt()) {
    out = value.get<int64_t>();
    return true;
  }
  if (value.bNumber()) {
    const double number = value.get<double>();
    if (number != static_cast<double>(static_cast<int64_t>(number))) {
      return false;
    }
    out = static_cast<int64_t>(number);
    return true;
  }
  return false;
}

bool readRowNumber(const Json& data, const char* key, int64_t& out) {
  return data.find(key) && numberValue(data[key], out);
}

// 校验并展开一行打包行。返回 nullopt = 这行不是打包行 (普通事件, 走 decodeEvent);
// 是打包行但形状非法时抛。展开语义与 dsh expandRow 逐字段一致: seq=seq0+k,
// time=time0+前 k 个 dt 之和, name (若有) 每个成员都带 (dsh 打包前提是 run 内均匀)。
std::optional<std::vector<SessionEvent>> tryExpandChunkRow(
    const std::string& line) {
  Json value;
  try {
    value = parserJson(line.c_str());
  } catch (...) {
    return std::nullopt;
  }
  if (!value.bObject() || !value.find("type") || !value["type"].bString()) {
    return std::nullopt;
  }
  const std::string tag = value["type"].get<std::string>();
  if (!isChunkRowTag(tag)) return std::nullopt;

  if (!hasExactKeys(value, {"type", "seq0", "time0", "data"})) {
    malformedRow(tag, "envelope must be exactly {type, seq0, time0, data}");
  }
  int64_t seq0 = 0;
  int64_t time0 = 0;
  if (!readRowNumber(value, "seq0", seq0) || seq0 < 0) {
    malformedRow(tag, "seq0 must be a non-negative integer");
  }
  if (!readRowNumber(value, "time0", time0)) {
    malformedRow(tag, "time0 must be an integer");
  }
  const Json& data = value["data"];
  if (!data.bObject()) malformedRow(tag, "data must be an object");

  bool withName = false;
  if (tag == "tool-call-chunks") {
    withName = hasExactKeys(data, {"turn", "step", "index", "id", "name", "dt", "args"});
    if (!withName
        && !hasExactKeys(data, {"turn", "step", "index", "id", "dt", "args"})) {
      malformedRow(tag,
                   "data must be exactly {turn, step, index, id, name?, dt, args}");
    }
    if (!data.find("id") || !data["id"].bString()) {
      malformedRow(tag, "id must be a string");
    }
    if (withName && (!data.find("name") || !data["name"].bString())) {
      malformedRow(tag, "name must be a string when present");
    }
  } else if (!hasExactKeys(data, {"turn", "step", "index", "dt", "texts"})) {
    malformedRow(tag, "data must be exactly {turn, step, index, dt, texts}");
  }

  int64_t turn = 0;
  int64_t step = 0;
  int64_t index = 0;
  if (!readRowNumber(data, "turn", turn) || !readRowNumber(data, "step", step)
      || !readRowNumber(data, "index", index)) {
    malformedRow(tag, "turn/step/index must be numbers");
  }
  const char* payloadKey = tag == "tool-call-chunks" ? "args" : "texts";
  if (!data.find(payloadKey) || !data[payloadKey].bArray()
      || data[payloadKey].size() == 0) {
    malformedRow(tag, std::string(payloadKey) + " must be a non-empty array");
  }
  const Json& payload = data[payloadKey];
  for (size_t k = 0; k < payload.size(); ++k) {
    if (!payload.at(k).bString()) {
      malformedRow(tag, std::string(payloadKey) + " must be a string array");
    }
  }
  if (!data.find("dt") || !data["dt"].bArray()) {
    malformedRow(tag, "dt must be an array");
  }
  const Json& gaps = data["dt"];
  std::vector<int64_t> dt;
  for (size_t k = 0; k < gaps.size(); ++k) {
    int64_t gap = 0;
    if (!numberValue(gaps.at(k), gap)) {
      malformedRow(tag, "dt must be an array of integers");
    }
    dt.push_back(gap);
  }
  if (dt.size() != payload.size() - 1) {
    malformedRow(tag, "dt length " + std::to_string(dt.size())
                        + " does not match " + std::to_string(payload.size())
                        + " members");
  }

  std::vector<SessionEvent> events;
  int64_t time = time0;
  for (size_t k = 0; k < payload.size(); ++k) {
    if (k > 0) time += dt[k - 1];
    SessionEvent event;
    event.type = EventType::AssistantChunk;
    event.seq = seq0 + static_cast<int64_t>(k);
    event.timeMs = time;
    if (tag == "text-chunks" || tag == "reasoning-chunks") {
      const std::string text = payload.at(k).get<std::string>();
      if (tag == "text-chunks") {
        event.data = AssistantChunkData{
            static_cast<int>(turn), static_cast<int>(step),
            StreamTextDelta{static_cast<int>(index), text}};
      } else {
        event.data = AssistantChunkData{
            static_cast<int>(turn), static_cast<int>(step),
            StreamReasoningDelta{static_cast<int>(index), text}};
      }
    } else {
      StreamToolCallDelta chunk;
      chunk.index = static_cast<int>(index);
      chunk.id = CallId(data["id"].get<std::string>());
      if (withName) chunk.name = data["name"].get<std::string>();
      chunk.argumentsDelta = payload.at(k).get<std::string>();
      event.data = AssistantChunkData{static_cast<int>(turn),
                                      static_cast<int>(step), std::move(chunk)};
    }
    events.push_back(std::move(event));
  }
  return events;
}

// ---------- 已有文件扫描 (attach 对齐用) ----------

// 数一行里存了多少条事件: 普通事件 1 条, 打包行 = 成员数。形状坏抛错。
size_t countLineEvents(const std::string& line) {
  Json value;
  try {
    value = parserJson(line.c_str());
  } catch (const std::exception&) {
    throw std::runtime_error("行不是合法 JSON: " + line.substr(0, 120));
  }
  if (!value.bObject() || !value.find("type") || !value["type"].bString()) {
    return 1;
  }
  const std::string tag = value["type"].get<std::string>();
  if (!isChunkRowTag(tag)) return 1;
  const Json& data = value.find("data") ? value["data"] : value;
  const char* payloadKey = tag == "tool-call-chunks" ? "args" : "texts";
  if (!data.bObject() || !data.find(payloadKey) || !data[payloadKey].bArray()) {
    throw std::runtime_error("打包行缺少 " + std::string(payloadKey) + " 数组");
  }
  const size_t members = data[payloadKey].size();
  if (members == 0) throw std::runtime_error("打包行成员数为 0");
  return members;
}

// 把文件切成完整行逐行数事件。committedBytes 是最后一个完整行结尾的偏移 ——
// 撕裂尾行 (无换行符收尾) 排除在外, attach 续写前要把它截掉。
struct FileScan {
  bool sawHeader = false;
  size_t eventCount = 0;
  int64_t committedBytes = 0;
  int64_t fileSize = 0;
};

FileScan scanStoredLines(const std::string& bytes) {
  FileScan scan;
  scan.fileSize = static_cast<int64_t>(bytes.size());
  size_t pos = 0;
  while (pos < bytes.size()) {
    const size_t newline = bytes.find('\n', pos);
    // 撕裂尾行 (无换行符收尾) 不属于已提交前缀 —— 连头行撕裂也一样: 什么都没提交,
    // attach 会先截掉再从头写。
    if (newline == std::string::npos) break;
    std::string line = bytes.substr(pos, newline - pos);
    pos = newline + 1;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (!line.empty()) {
      if (!scan.sawHeader) {
        // 首个非空行是头行, 不计事件。内容是否合法留给 loadSession 的解码。
        scan.sawHeader = true;
      } else {
        scan.eventCount += countLineEvents(line);
      }
    }
    scan.committedBytes = static_cast<int64_t>(pos);
  }
  return scan;
}

// 截掉文件尾部的撕裂残字节, 让 attach 能干净续写。
void truncateTo(const std::string& path, int64_t size) {
#ifdef _WIN32
  std::FILE* file = nullptr;
  fopen_s(&file, path.c_str(), "rb+");
  if (file == nullptr) {
    throw std::runtime_error("无法打开以截断撕裂尾行: " + path);
  }
  const int ok = _chsize_s(_fileno(file), static_cast<long>(size));
  std::fclose(file);
  if (ok != 0) {
    throw std::runtime_error("截断撕裂尾行失败: " + path);
  }
#else
  if (::truncate(path.c_str(), static_cast<off_t>(size)) != 0) {
    throw std::runtime_error("截断撕裂尾行失败: " + path);
  }
#endif
}

// 定位崩溃遗留的未闭合尾部。
struct OpenTail {
  bool hasOpenTurn = false;
  int turn = 0;
  // turn 内是否还有未闭合的 step。
  bool hasOpenStep = false;
  int step = 0;
};

OpenTail findOpenTail(const std::vector<SessionEvent>& events) {
  OpenTail tail;
  for (const SessionEvent& event : events) {
    switch (event.type) {
      case EventType::TurnStart:
        tail.hasOpenTurn = true;
        tail.turn = std::get<TurnStartData>(event.data).turn;
        // 新 turn 开始意味着上一个 turn 的 step 都已收尾 (否则日志本身就坏了,
        // 那种情况留给 seed 校验去拒绝)。
        tail.hasOpenStep = false;
        break;
      case EventType::TurnEnd:
        tail.hasOpenTurn = false;
        tail.hasOpenStep = false;
        break;
      case EventType::StepStart:
        tail.hasOpenStep = true;
        tail.step = std::get<StepStartData>(event.data).step;
        break;
      case EventType::StepEnd:
        tail.hasOpenStep = false;
        break;
      default:
        break;
    }
  }
  return tail;
}

// 给未闭合的尾部补写收尾标记。
//
// 顺序必须是先 step/end 再 turn/end —— 与驱动正常退出时 RAII 析构的顺序一致, 否则
// 回放出来的边界嵌套是错的。
void repairOpenTail(std::vector<SessionEvent>& events, bool& repaired) {
  const OpenTail tail = findOpenTail(events);
  if (!tail.hasOpenTurn) return;

  const int64_t time = nowMillis();
  if (tail.hasOpenStep) {
    SessionEvent stepEnd;
    stepEnd.type = EventType::StepEnd;
    stepEnd.seq = events.size();
    stepEnd.timeMs = time;
    stepEnd.data = StepEndData{tail.turn, tail.step};
    events.push_back(std::move(stepEnd));
  }

  SessionEvent turnEnd;
  turnEnd.type = EventType::TurnEnd;
  turnEnd.seq = events.size();
  turnEnd.timeMs = time;
  turnEnd.data = TurnEndData{tail.turn, TurnEndInterrupted{}};
  events.push_back(std::move(turnEnd));
  repaired = true;
}

}  // namespace

// ===========================================================================
// SessionWriter
// ===========================================================================

SessionWriter::~SessionWriter() { detach(); }

bool SessionWriter::attach(Session& session, const std::string& path) {
  detach();

  std::lock_guard<std::mutex> lock(mtx);
  std::optional<std::string> existing;
  std::optional<FileScan> scan;
  try {
    existing = readAllBytes(path);
    if (existing.has_value()) {
      if (hasZstdMagic(*existing)) {
        throw std::runtime_error("会话日志 " + path
                                 + " 是 zstd 压缩档; avox 只读写明文 .jsonl");
      }
      scan = scanStoredLines(*existing);
    }
  } catch (const std::exception& e) {
    errorText = std::string("会话日志已存在但无法对齐: ") + e.what();
    LOGFLF(LogLevel::warn, "[session] ", errorText.c_str());
    return false;
  }

  const bool isNew = !scan.has_value() || !scan->sawHeader;
  if (scan.has_value() && scan->fileSize > scan->committedBytes) {
    // 撕裂尾行: 直接 append 会把新事件拼进残行, 必须先截掉 (dsh 的 committedBytes
    // 截断同款语义)。
    const int64_t torn = scan->fileSize - scan->committedBytes;
    try {
      truncateTo(path, scan->committedBytes);
    } catch (const std::exception& e) {
      errorText = e.what();
      LOGFLF(LogLevel::warn, "[session] ", errorText.c_str());
      return false;
    }
    LOGFLF(LogLevel::warn, "[session] 已截掉 ", std::to_string(torn).c_str(),
           " 字节撕裂尾行: ", path.c_str());
  }

  file.open(path, std::ios::binary | std::ios::app);
  if (!file.is_open()) {
    errorText = "无法打开会话日志文件: " + path;
    LOGFLF(LogLevel::warn, "[session] ", errorText.c_str());
    return false;
  }
  filePath = path;
  errorText.clear();

  if (isNew) writeLine(encodeHeader(session.getHeader()));

  // 只补写文件里还没有的部分 (按事件数对齐, 打包行按成员数计)。
  const size_t written = scan.has_value() ? scan->eventCount : 0;
  const std::vector<SessionEvent>& events = session.events();
  for (size_t seq = written; seq < events.size(); ++seq) {
    writeLine(encodeEvent(events[seq]));
  }
  file.flush();

  attached = &session;
  session.addObserver(this);
  return true;
}

void SessionWriter::detach() {
  Session* target = nullptr;
  {
    std::lock_guard<std::mutex> lock(mtx);
    target = attached;
    attached = nullptr;
  }
  // 注销放在锁外: removeObserver 不会回调进本对象, 但持锁调外部对象是死锁的常见来源。
  if (target != nullptr) target->removeObserver(this);

  std::lock_guard<std::mutex> lock(mtx);
  if (file.is_open()) {
    file.flush();
    file.close();
  }
  filePath.clear();
}

void SessionWriter::flush() {
  std::lock_guard<std::mutex> lock(mtx);
  if (file.is_open()) file.flush();
}

void SessionWriter::onSessionEvent(const Session& session,
                                   const SessionEvent& event) {
  (void)session;
  std::lock_guard<std::mutex> lock(mtx);
  if (!file.is_open()) return;
  writeLine(encodeEvent(event));
  // 分片不刷盘: 它们量大 (一次回复几百条), 而丢失只影响 token 级回放保真, 不影响模型
  // 历史重建 —— 后者靠 assistant/message。其余事件立即刷, 于是任何崩溃最多丢掉最后
  // 一次回复的逐字回放, 派生历史与边界结构完整。
  if (event.type != EventType::AssistantChunk) file.flush();
}

void SessionWriter::writeLine(const std::string& line) {
  file.write(line.data(), static_cast<std::streamsize>(line.size()));
  file.put('\n');
  if (!file.good()) {
    errorText = "写入会话日志失败: " + filePath;
    LOGFLF(LogLevel::warn, "[session] ", errorText.c_str());
  }
}

// ===========================================================================
// 加载
// ===========================================================================

LoadedSession loadSession(const std::string& path) {
  const std::optional<std::string> bytes = readAllBytes(path);
  if (!bytes.has_value()) {
    throw std::runtime_error("会话日志文件不存在或无法读取: " + path);
  }
  if (hasZstdMagic(*bytes)) {
    throw std::runtime_error(
        "会话日志 " + path
        + " 是 zstd 压缩档; avox 全明文 (用户决策), 请在 dsh 侧配置 "
          "compression:'none' 后重录, 或解压该文件为 .jsonl");
  }

  LoadedSession loaded;
  size_t lineNumber = 0;
  bool headerRead = false;
  // 逐行处理; 撕裂的尾行 (无换行符收尾) 丢弃 —— dsh 的 SessionLogScanner 同款
  // 语义, 崩溃期写了一半的行不属于已提交前缀。
  size_t pos = 0;
  bool tornTail = false;
  while (pos < bytes->size()) {
    const size_t newline = bytes->find('\n', pos);
    const bool complete = newline != std::string::npos;
    const size_t end = complete ? newline : bytes->size();
    std::string line = bytes->substr(pos, end - pos);
    pos = complete ? newline + 1 : bytes->size();
    ++lineNumber;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) continue;
    if (!complete) {
      tornTail = true;
      break;
    }

    try {
      if (!headerRead) {
        loaded.header = decodeHeader(line);
        headerRead = true;
        continue;
      }
      // 打包存储行先展开; 不是打包行 (nullopt) 才走常规事件解码。
      if (std::optional<std::vector<SessionEvent>> expanded =
              tryExpandChunkRow(line);
          expanded.has_value()) {
        for (SessionEvent& event : *expanded) {
          loaded.events.push_back(std::move(event));
        }
        continue;
      }
      DecodedEvent decoded = decodeEvent(line);
      if (decoded.status == DecodedEvent::Status::SkippedIgnorable) {
        ++loaded.skippedIgnorable;
        continue;
      }
      loaded.events.push_back(std::move(decoded.event));
    } catch (const std::exception& e) {
      throw std::runtime_error("会话日志 " + path + " 第 "
                               + std::to_string(lineNumber) + " 行: " + e.what());
    }
  }

  if (!headerRead) {
    throw std::runtime_error("会话日志 " + path + " 缺少元数据行");
  }
  if (tornTail) {
    LOGFLF(LogLevel::warn, "[session] ", path.c_str(),
           " 尾部有撕裂行 (无换行符收尾), 已按 dsh 语义丢弃该行");
  }

  // seq 连续性: 跳过 ignorable 事件会让后续 seq 出现空洞, 而 seq == 下标 是全系统依赖
  // 的契约。出现空洞就意味着这份日志对当前运行时不可用 —— 明确拒绝, 而不是悄悄重编号
  // (重编号会让 sourceEventSeqs 与 surfaceOp 的区间全部指向错误的位置)。
  for (size_t index = 0; index < loaded.events.size(); ++index) {
    if (loaded.events[index].seq == index) continue;
    throw std::runtime_error(
        "会话日志 " + path + " 的 seq 不连续: 第 " + std::to_string(index)
        + " 条事件的 seq 是 " + std::to_string(loaded.events[index].seq)
        + (loaded.skippedIgnorable > 0
               ? " (已跳过 " + std::to_string(loaded.skippedIgnorable)
                     + " 条 ignorable 事件, 空洞由此产生)"
               : ""));
  }

  repairOpenTail(loaded.events, loaded.repairedInterruptedTail);
  if (loaded.repairedInterruptedTail) {
    LOGFLF(LogLevel::warn, "[session] ", path.c_str(),
           " 存在崩溃遗留的未闭合 turn, 已补写 interrupted 收尾");
  }
  return loaded;
}

}
