#include "AgentExport.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "AgentShell.hpp"
#include "adapter/ProviderCatalog.hpp"
#include "avox/Avox.hpp"  // getAvoxPath
#include "avox/module/AssetLoader.hpp"
#include "avox/module/Json.hpp"
#include "avox/module/LogHelper.hpp"
#include "avox_agent/compose/DiagnosticAgent.hpp"
#include "avox_agent/core/SessionCodec.hpp"

namespace avox {

namespace {

// createAgentHost 的失败原因 (线程局部: 两个线程各自的失败不该互相覆盖)。
thread_local std::string g_lastCreateError;

// followupImages 用: 标准 base64 解码 (容忍换行与结尾 '=')。失败返回空串。
// 与 vision-toolkit/VisionHttp.cpp 里那份同款 —— 各留一份避免插件内部件外泄。
std::vector<uint8_t> base64Decode(const std::string& input) {
  auto unmap = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
  };
  std::vector<uint8_t> out;
  out.reserve(input.size() / 4 * 3);
  int32_t val = 0;
  int32_t valb = -8;
  for (char c : input) {
    if (c == '=' || c == '\n' || c == '\r' || c == ' ' || c == '\t') continue;
    const int d = unmap(c);
    if (d < 0) return {};
    val = (val << 6) + d;
    valb += 6;
    if (valb >= 0) {
      out.push_back(static_cast<uint8_t>((val >> valb) & 0xFF));
      valb -= 8;
    }
  }
  return out;
}


// 把 C++ 会话事件转成导出层的 JSON 回调。
//
// 一个适配器服务全部观察者: 编码只做一次, 而不是每个观察者各编一遍。
class ObserverBridge : public SessionObserver {
 public:
  void add(ISessionObserver* observer) {
    if (observer == nullptr) return;
    std::lock_guard<std::mutex> lock(mtx);
    if (std::find(observers.begin(), observers.end(), observer)
        != observers.end()) {
      return;
    }
    observers.push_back(observer);
  }

  void remove(ISessionObserver* observer) {
    std::lock_guard<std::mutex> lock(mtx);
    observers.erase(std::remove(observers.begin(), observers.end(), observer),
                    observers.end());
  }

  void onSessionEvent(const Session& session, const SessionEvent& event) override {
    (void)session;
    std::vector<ISessionObserver*> snapshot;
    {
      std::lock_guard<std::mutex> lock(mtx);
      if (observers.empty()) return;
      snapshot = observers;
    }

    // 便利回调走快路径。它们**不替代** onSessionEvent —— 同一条事件两边都会到, 于是只订阅
    // JSON 通道的绑定不会漏掉任何东西, 而只要卡片的绑定不必解析 JSON。
    //
    // 每个回调都各自 try/catch: 事件已经提交, 一个绑定的失败不能影响其余绑定。
    dispatchConvenience(event, snapshot);

    std::string payload;
    try {
      // 只编码 data 部分: seq 与 typeName 单独作参数传, 省掉绑定层再解一层的成本。
      const std::string line = encodeEvent(event);
      payload = line;
    } catch (...) {
      return;
    }
    const char* typeName = eventTypeName(event.type);
    for (ISessionObserver* observer : snapshot) {
      try {
        observer->onSessionEvent(event.seq, typeName, payload.c_str());
      } catch (...) {
      }
    }
  }

  void notifyStatus(int status) {
    std::vector<ISessionObserver*> snapshot;
    {
      std::lock_guard<std::mutex> lock(mtx);
      snapshot = observers;
    }
    for (ISessionObserver* observer : snapshot) {
      try {
        observer->onStatus(status);
      } catch (...) {
      }
    }
  }

 private:
  // 把一条事件翻译成便利回调。
  void dispatchConvenience(const SessionEvent& event,
                          const std::vector<ISessionObserver*>& observers) {
    switch (event.type) {
      case EventType::AssistantChunk: {
        const auto& data = std::get<AssistantChunkData>(event.data);
        // 只有 delta 分片驱动便利回调; block 边界/usage/finish 走 JSON 通道。
        if (const auto* delta = std::get_if<StreamTextDelta>(&data.chunk)) {
          notifyEach(observers, [&](ISessionObserver* observer) {
            observer->onToken(delta->text.c_str());
          });
        } else if (const auto* thinking =
                       std::get_if<StreamReasoningDelta>(&data.chunk)) {
          notifyEach(observers, [&](ISessionObserver* observer) {
            observer->onReasoning(thinking->text.c_str());
          });
        }
        return;
      }

      case EventType::AssistantMessageEvent: {
        // 记下本轮最后一条助手回复, turn/end 时作为 content 交出去。
        const auto& data = std::get<AssistantMessageData>(event.data);
        std::string text;
        for (const ContentBlock& block : data.message.content) {
          if (const auto* t = std::get_if<TextBlock>(&block)) text += t->text;
        }
        if (!text.empty()) lastAssistantText = std::move(text);
        return;
      }

      case EventType::ToolCall: {
        const auto& data = std::get<ToolCallData>(event.data);
        // 记 callId → name: tool/result 事件只带 callId, 而 UI 要显示工具名。
        callNames[data.callId.value] = data.name;
        notifyEach(observers, [&](ISessionObserver* observer) {
          observer->onToolCall(data.name.c_str(), data.arguments.c_str());
        });
        return;
      }

      case EventType::ToolResult: {
        const auto& data = std::get<ToolResultData>(event.data);
        std::string text;
        bool isError = false;
        // 消息 content 是 ToolResultBlock 列表 (恰好一个), 块内才是正文条目;
        // callId 在 source 上 (dsh 形)。
        for (const ToolResultBlock& block : data.message.content) {
          isError = isError || block.isError;
          for (const ToolResultContent& item : block.content) {
            if (const auto* t = std::get_if<TextBlock>(&item)) text += t->text;
          }
        }
        std::string name;
        if (data.message.source.callId.has_value()) {
          const auto found = callNames.find(data.message.source.callId->value);
          if (found != callNames.end()) name = found->second;
        }
        const bool ok = !isError;
        notifyEach(observers, [&](ISessionObserver* observer) {
          observer->onToolResult(name.c_str(), text.c_str(), ok);
        });
        return;
      }

      case EventType::TurnEnd: {
        const auto& data = std::get<TurnEndData>(event.data);
        std::string error;
        if (const auto* failed = std::get_if<TurnEndError>(&data.reason)) {
          error = failed->error.message;
        } else if (std::holds_alternative<TurnEndAborted>(data.reason)) {
          error = "aborted";
        }
        const std::string content = lastAssistantText;
        notifyEach(observers, [&](ISessionObserver* observer) {
          observer->onTurnEnd(content.c_str(), error.c_str());
        });
        lastAssistantText.clear();
        callNames.clear();
        return;
      }

      default:
        return;
    }
  }

  template <class Fn>
  static void notifyEach(const std::vector<ISessionObserver*>& observers, Fn&& fn) {
    for (ISessionObserver* observer : observers) {
      try {
        fn(observer);
      } catch (...) {
      }
    }
  }

  std::mutex mtx;
  std::vector<ISessionObserver*> observers;
  // 以下两项只由会话回调线程访问 (driver 线程串行), 不需要额外加锁。
  std::string lastAssistantText;
  std::map<std::string, std::string> callNames;
};

class SessionAdapter : public IAgentSession {
 public:
  // attachments: 附件仓借用指针 (由 HostAdapter::composed 持有, 寿命长于本适配器)。
  // followupImages 的图片准入 (sha256 入库 + 限额校验) 都靠它。
  SessionAdapter(Agent* agent, std::string trackPathValue,
                 AttachmentStore* attachments)
      : agent(agent), trackPathValue(std::move(trackPathValue)),
        attachments(attachments) {
    agent->session().addObserver(&bridge);
  }

  ~SessionAdapter() override {
    // 观察者必须在 agent 停下来之后才摘 —— 由 AgentHost::closeAgent 保证顺序,
    // 这里只做解绑。
    agent->session().removeObserver(&bridge);
  }

  void followup(const char* text) override {
    agent->followup(makeMessage(text, "followup"));
  }
  void steer(const char* text) override {
    agent->steer(makeMessage(text, "steer"));
  }
  void inject(const char* text) override {
    agent->inject(makeMessage(text, "inject"));
  }

  int followupImages(const char* text, const char* imagesJson) override {
    lastInputErrorText.clear();
    try {
      if (attachments == nullptr) {
        throw std::runtime_error("会话未装配附件仓, 无法接收图片");
      }
      const std::string json = imagesJson == nullptr ? "" : imagesJson;
      Json images;
      try {
        images = parserJson(json.c_str());
      } catch (const std::exception& e) {
        throw std::runtime_error(std::string("images 解析失败: ") + e.what());
      }
      if (!images.bArray()) {
        throw std::runtime_error("images 必须是数组");
      }
      // 批量准入预检 (dsh saveImages 同义): 条数与总字节按配置限额卡在提交前,
      // 避免逐图 publish 到一半才发现超限留下半截状态。
      const ImageAdmissionLimits limits = attachments->admissionLimits();
      if (images.size() > static_cast<size_t>(limits.maxImagesPerMessage)) {
        throw std::runtime_error("单条消息图片数超限: "
                                 + std::to_string(images.size()) + " > "
                                 + std::to_string(limits.maxImagesPerMessage));
      }
      int64_t totalBytes = 0;
      for (size_t i = 0; i < images.size(); ++i) {
        const Json& item = images[i];
        if (!item.bObject()) throw std::runtime_error("images[" + std::to_string(i) + "] 不是对象");
        if (!item.find("data") || !item["data"].bString()) {
          throw std::runtime_error("images[" + std::to_string(i) + "] 缺少 data");
        }
        // base64 体积下限的原始字节数 ≈ len*3/4。
        totalBytes += static_cast<int64_t>(item["data"].get<std::string>().size()) * 3 / 4;
      }
      if (totalBytes > limits.maxMessageImageBytes) {
        throw std::runtime_error("单条消息图片总字节超限: "
                                 + std::to_string(totalBytes) + " > "
                                 + std::to_string(limits.maxMessageImageBytes));
      }
      UserMessage message = makeMessage(text, "followup");
      for (size_t i = 0; i < images.size(); ++i) {
        const Json& item = images[i];
        if (!item.bObject()) throw std::runtime_error("images[" + std::to_string(i) + "] 不是对象");
        const std::string data = readImageString(item, "data", i);
        const std::string mediaType = readImageString(item, "mediaType", i);
        std::string name;
        if (item.find("name") && item["name"].bString()) {
          name = item["name"].get<std::string>();
        }
        if (mediaType.rfind("image/", 0) != 0) {
          throw std::runtime_error("images[" + std::to_string(i)
                                   + "] mediaType 非法: " + mediaType);
        }
        const std::vector<uint8_t> bytes = base64Decode(data);
        if (bytes.empty()) {
          throw std::runtime_error("images[" + std::to_string(i)
                                   + "] base64 解码失败");
        }
        message.content.push_back(ImageBlock{attachments->publish(
            bytes.data(), bytes.size(), mediaType,
            name.empty() ? std::nullopt : std::optional<std::string>(name))});
      }
      agent->followup(std::move(message));
      return 0;
    } catch (const std::exception& e) {
      lastInputErrorText = e.what();
      LOGFLF(LogLevel::warn, "[export] followupImages 失败: ", e.what());
      return 1;
    }
  }

  const char* lastInputError() override { return lastInputErrorText.c_str(); }

  void cancel(int cause) override {
    switch (cause) {
      case 1: agent->cancel(AgentCancelCause{CancelByParent{}}); return;
      case 2: agent->cancel(AgentCancelCause{CancelByHook{"external"}}); return;
      case 3: agent->cancel(AgentCancelCause{CancelByDisposed{}}); return;
      case 0:
      default: agent->cancel(AgentCancelCause{CancelByUser{}}); return;
    }
  }

  bool waitIdle(int timeoutMs) override { return agent->whenIdle(timeoutMs); }

  int status() override {
    return agent->status() == AgentStatus::Running ? 1 : 0;
  }

  bool compactNow() override {
    // 压缩策略挂在 pre-step 上, 所以这里只需要占一次维护相位让它有机会跑 ——
    // 真正的压缩在下一个 step 的 pre-step 里发生。
    return agent->runMaintenance([](const std::shared_ptr<AbortSignal>&) {});
  }

  void addObserver(ISessionObserver* observer) override { bridge.add(observer); }
  void removeObserver(ISessionObserver* observer) override {
    bridge.remove(observer);
  }

  size_t eventCount() override {
    size_t count = 0;
    agent->withSession([&](Session& session) { count = session.events().size(); });
    return count;
  }

  const char* eventJson(size_t seq) override {
    scratch.clear();
    agent->withSession([&](Session& session) {
      const std::vector<SessionEvent>& events = session.events();
      if (seq >= events.size()) return;
      try {
        scratch = encodeEvent(events[seq]);
      } catch (...) {
        scratch.clear();
      }
    });
    return scratch.c_str();
  }

  const char* trackPath() override { return trackPathValue.c_str(); }

 private:
  // images[i] 的字符串字段读取 (data / mediaType)。缺字段按空串处理, 由调用方报错。
  static std::string readImageString(const Json& item, const char* key,
                                     size_t index) {
    if (!item.find(key) || !item[key].bString()) {
      throw std::runtime_error("images[" + std::to_string(index)
                               + "] 缺少 " + key);
    }
    return item[key].get<std::string>();
  }

  UserMessage makeMessage(const char* text, const char* kind) {
    UserMessage message;
    message.id = MessageId(agent->id().value + "/" + kind + "/"
                           + std::to_string(++counter));
    message.content.push_back(TextBlock{text == nullptr ? "" : text});
    // 真人输入标 User: 循环卫生策略靠这个判定「用户插话过」并清空重复计数链。
    message.source.kind = MessageSourceKind::User;
    return message;
  }

  Agent* agent;
  std::string trackPathValue;
  ObserverBridge bridge;
  AttachmentStore* attachments = nullptr;
  // followupImages 的失败原因 (lastInputError 返回的借用缓冲)。
  std::string lastInputErrorText;
  // eventJson 的返回缓冲 (每个适配器一份, 下次调用即失效)。
  std::string scratch;
  size_t counter = 0;
};

class HostAdapter : public IAgentHost {
 public:
  // 装配走 composeDiagnosticAgent —— 与 AgentShell 同一个入口, 于是「装配步骤与顺序」
  // 这份知识只存在一处。
  static std::unique_ptr<HostAdapter> create(const std::string& configJson,
                                            std::string& error) {
    auto adapter = std::unique_ptr<HostAdapter>(new HostAdapter());
    if (!composeDiagnosticAgent(adapter->composed, configJson,
                                getAvoxPath() + "/logs", error)) {
      return nullptr;
    }
    return adapter;
  }

  ~HostAdapter() override { shutdown(); }

  void shutdown() override {
    closeAgent();
    composed.reset();
  }

  IAgentSession* openAgent(const char* sessionId) override {
    errorText.clear();
    if (composed.host == nullptr) {
      errorText = "宿主未装配";
      return nullptr;
    }
    if (session != nullptr) {
      errorText = "已有打开的会话";
      return nullptr;
    }
    try {
      Agent* agent = composed.host->openAgent(sessionId == nullptr ? "" : sessionId);
      session = std::make_unique<SessionAdapter>(agent, composed.host->sessionPath(),
                                                 composed.attachments.get());
      return session.get();
    } catch (const std::exception& e) {
      errorText = e.what();
      LOGFLF(LogLevel::warn, "[export] openAgent 失败: ", e.what());
      return nullptr;
    }
  }

  void closeAgent() override {
    // 先摘适配器 (它持有 session 的观察者), 再让 host 停驱动 ——
    // 反过来会让驱动的收尾事件打进一个正在析构的桥。
    session.reset();
    if (composed.host != nullptr) composed.host->closeAgent();
  }

  bool addPromptSection(const char* name, int order, const char* text) override {
    errorText.clear();
    if (composed.host == nullptr) {
      errorText = "宿主未装配";
      return false;
    }
    try {
      composed.registrations.push_back(composed.host->addPromptSection(
          name == nullptr ? "" : name, order, text == nullptr ? "" : text));
      return true;
    } catch (const std::exception& e) {
      errorText = e.what();
      return false;
    }
  }

  void setApprovalUi(IApprovalUi* ui) override {
    if (composed.host == nullptr) return;
    if (ui == nullptr) {
      composed.host->setApprovalAnswerer(nullptr);
      return;
    }
    composed.host->setApprovalAnswerer([ui](const ApprovalRequest& request) {
      int answer = 3;
      try {
        answer = ui->ask(request.toolName.c_str(), request.callId.value.c_str(),
                         request.reason.c_str());
      } catch (...) {
        // 应答方抛异常按不可用处理 —— fail closed。
        answer = 3;
      }
      switch (answer) {
        case 0: return ApprovalOutcome::AllowedOnce;
        case 1: return ApprovalOutcome::Rejected;
        case 2: return ApprovalOutcome::Cancelled;
        default: return ApprovalOutcome::Unavailable;
      }
    });
  }

  const char* lastError() override { return errorText.c_str(); }

 private:
  HostAdapter() = default;

  ComposedAgent composed;
  std::unique_ptr<SessionAdapter> session;
  std::string errorText;
};

}  // namespace

extern "C" {

IAgentHost* createAgentHost(const char* configJson) {
  g_lastCreateError.clear();
  try {
    std::string error;
    std::unique_ptr<HostAdapter> adapter =
        HostAdapter::create(configJson == nullptr ? "{}" : configJson, error);
    if (adapter == nullptr) {
      // 配错响亮地失败: 返回 nullptr 并把原因留下, 而不是带着半套装配继续跑。
      g_lastCreateError = error;
      LOGFLF(LogLevel::warn, "[export] createAgentHost 失败: ", error.c_str());
      return nullptr;
    }
    return adapter.release();
  } catch (const std::exception& e) {
    g_lastCreateError = e.what();
    LOGFLF(LogLevel::warn, "[export] createAgentHost 失败: ", e.what());
    return nullptr;
  } catch (...) {
    g_lastCreateError = "未知异常";
    return nullptr;
  }
}

const char* lastAgentHostError() { return g_lastCreateError.c_str(); }

int agentShellRun() { return AgentShell::run(); }

// avoxListProviders: 列出 chat=true 的条目, 输出与 AgentShell /list 等价的 JSON 数组。
//
// 输出 schema 注释见 AgentExport.h。这里只在 C++ 端手搓最简 JSON (节点少, 不值得再开
// JsonWriter); 字符串用 [\"\\\b\f\n\r\t] 规则转义。
namespace {

std::string escapeJsonString(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 2);
  for (char c : s) {
    switch (c) {
      case '"':  out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b";  break;
      case '\f': out += "\\f";  break;
      case '\n': out += "\\n";  break;
      case '\r': out += "\\r";  break;
      case '\t': out += "\\t";  break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
          out += buf;
        } else {
          out += c;
        }
    }
  }
  return out;
}

}  // namespace

const char* avoxListProviders() {
  // 静态缓冲: 调用方拿到的是 const char*, 下次调用即失效。
  static thread_local std::string g_providersJson;

  std::vector<ProviderEntry> entries;
  // 优先读 providers.json; 缺/空时回退内建免费目录, 与 AgentShell::listCatalogEntries 一致。
  const auto raw = AssetLoader::loadToMemory("config/providers.json");
  if (!raw.empty()) {
    try {
      Json root = parserJson(std::string(raw.begin(), raw.end()).c_str());
      if (root.bObject()) {
        ProviderCatalog catalog;
        catalog.load(root);
        if (catalog.hasProviders()) entries = catalog.entries();
      }
    } catch (...) {
      // 解析失败回退内建, 至少保证前端不空。
    }
  }
  if (entries.empty()) entries = defaultFreeProviders();

  std::ostringstream os;
  os << '[';
  bool first = true;
  for (const ProviderEntry& e : entries) {
    // /list 选的是「对话后端」, 纯视觉与文生图模型不进。
    if (!e.chat) continue;
    if (!first) os << ',';
    first = false;
    os << '{'
       << "\"provider\":"        << '"' << escapeJsonString(e.provider) << "\","
       << "\"model\":"           << '"' << escapeJsonString(e.model)    << "\","
       << "\"name\":"            << '"' << escapeJsonString(e.name)     << "\","
       << "\"free\":"            << (e.free ? "true" : "false") << ','
       << "\"ready\":"           << (e.ready() ? "true" : "false") << ','
       << "\"needsApiKey\":"     << (e.needsApiKey() ? "true" : "false") << ','
       << "\"apiKeyHint\":"      << '"' << escapeJsonString(e.apiKeyHint()) << "\","
       << "\"apiUrl\":"          << '"' << escapeJsonString(e.apiUrl)  << "\","
       << "\"apiPath\":"         << '"' << escapeJsonString(e.apiPath) << "\","
       << "\"contextWindow\":"   << e.contextWindow << ','
       << "\"imageInput\":"      << (e.imageInput  ? "true" : "false") << ','
       << "\"imageOutput\":"     << (e.imageOutput ? "true" : "false")
       << '}';
  }
  os << ']';
  g_providersJson = os.str();
  return g_providersJson.c_str();
}
}

}
