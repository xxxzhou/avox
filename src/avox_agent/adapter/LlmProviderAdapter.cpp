#include "LlmProviderAdapter.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <map>
#include <stdexcept>
#include <utility>

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#else
#include <sys/socket.h>
#endif

#include "avox/module/Json.hpp"
#include "avox/module/LogHelper.hpp"
#include "avox/module/Sha256.hpp"
#include "avox/module/Utf8.hpp"
#include "avox_agent/provider/ProviderFactory.hpp"
#include "avox_agent/transport/HttplibTransport.hpp"

namespace avox {

namespace {

// 从 https://host/api/v1 拆出 baseUrl 与 path 段。
void splitUrl(const std::string& url, std::string& baseUrl, std::string& pathPart) {
  const size_t schemeEnd = url.find("://");
  if (schemeEnd == std::string::npos) {
    baseUrl = url;
    pathPart.clear();
    return;
  }
  const size_t pathStart = url.find('/', schemeEnd + 3);
  if (pathStart == std::string::npos) {
    baseUrl = url;
    pathPart.clear();
    return;
  }
  baseUrl = url.substr(0, pathStart);
  pathPart = url.substr(pathStart);
}

// HTTP 状态 → 语义失败码。
//
// ModelRoutePolicy 认的是这些码而不是 HTTP 数字: 换后端时策略不用改, 而「连接失败」与
// 「限流」的处置本来就不同 (换模型对网络问题没用)。
std::string codeOfStatus(int status) {
  if (status == 429 || status == 402) return "RATE_LIMITED";
  if (status >= 500) return "UPSTREAM_ERROR";
  if (status == 401 || status == 403) return "AUTH_FAILED";
  if (status == 400) return "BAD_REQUEST";
  return "HTTP_ERROR";
}

// ImageBlock -> data URL (OpenAI 兼容 wire 的图片形态)。
//
// 日志里只有附件引用, 字节从附件仓取 (dsh attachment 模型); 没有仓或对象缺失都是
// 配置级错误 —— 静默丢图等于对模型撒谎, 必须响亮失败。
std::string imageUrlOf(const ImageBlock& block, AttachmentStore* store) {
  if (store == nullptr) {
    throw std::runtime_error("消息含图片块但没有附件仓 (ImageBlock 需要 "
                             "AttachmentStore 还原字节)");
  }
  const std::vector<uint8_t> bytes = store->load(block.attachment);
  return "data:" + block.attachment.mediaType + ";base64,"
         + base64Encode(bytes);
}

// 把新架构的派生历史转成 Provider 的语义消息。
ProviderRequest toProviderRequest(const LlmRequest& request,
                                 const LlmProviderAdapter::Config& config) {
  ProviderRequest req;
  req.model = request.config.model.empty() ? config.model : request.config.model;
  req.temperature = request.config.temperature.value_or(config.temperature);
  req.maxTokens = request.config.maxTokens.value_or(config.maxTokens);
  // reasoning_effort 走 OpenAI 兼容 wire, ChatProvider 在 req.reasoningEffort 非空时发出。
  // 物化默认值 "medium" 来自 Config.reasoningEffort (默认 medium); 用户/UI 在选模型时
  // 显式设的值会盖过它。
  req.reasoningEffort =
      request.config.reasoningEffort.value_or(config.reasoningEffort);
  req.systemPrompt = request.system;
  req.toolsDefinitionJson = request.toolsJson;

  for (const Message& message : request.messages) {
    ProviderMessage out;
    if (const auto* user = std::get_if<UserMessage>(&message)) {
      out.role = ProviderRole::User;
      for (const ContentBlock& block : user->content) {
        if (const auto* text = std::get_if<TextBlock>(&block)) {
          out.parts.push_back(ProviderPart{PartType::Text, text->text});
        } else if (const auto* image = std::get_if<ImageBlock>(&block)) {
          out.parts.push_back(
              ProviderPart{PartType::ImageUrl, imageUrlOf(*image, config.attachmentStore)});
        }
      }
      // 空消息不发: 后端对空 content 的容忍度各异, 而空消息本身没有信息。
      if (out.parts.empty()) continue;
      req.messages.push_back(std::move(out));
      continue;
    }

    if (const auto* assistant = std::get_if<AssistantMessage>(&message)) {
      out.role = ProviderRole::Assistant;
      std::string text;
      std::string reasoning;
      Json toolCalls(Json::JsonArray{});
      for (const ContentBlock& block : assistant->content) {
        if (const auto* t = std::get_if<TextBlock>(&block)) {
          text += t->text;
        } else if (const auto* call = std::get_if<ToolCallBlock>(&block)) {
          // 生成 OpenAI 兼容的 tool_calls wire。
          //
          // 后端严格要求 assistant.tool_calls[].id 与每条 role:tool 的 tool_call_id
          // 一一对应, 否则续发直接 400 —— 而 id 来自流式期间的 index→id 映射。
          Json entry(Json::JsonObject{});
          entry["id"] = call->id.value;
          entry["type"] = "function";
          Json function(Json::JsonObject{});
          function["name"] = call->name;
          // 空 arguments 补 {}: 后端不接受空字符串。
          function["arguments"] =
              call->arguments.empty() ? std::string("{}") : call->arguments;
          entry["function"] = std::move(function);
          toolCalls.push_back(std::move(entry));
        } else if (const auto* reason = std::get_if<ReasoningBlock>(&block)) {
          // CoT 回传 (dsh #2786): 每个带推理的轮都送回 —— tool-call 轮官方必填,
          // 其余轮后端忽略但网关转码要靠它恢复思维链签名。
          reasoning += reason->text;
        } else if (std::get_if<ImageBlock>(&block) != nullptr) {
          // dsh assertSupportedImageRoles: assistant 消息的 wire 形态带不了图。
          // 静默跳过等于悄悄丢掉模型已见过的输入 —— 响亮失败。
          throw std::runtime_error(
              "assistant 消息含图片块, OpenAI 兼容 wire 无法表示");
        }
      }
      if (toolCalls.size() > 0) out.assistantToolCallsWire = toolCalls.dump();
      if (!reasoning.empty()) out.assistantReasoning = reasoning;
      out.parts.push_back(ProviderPart{PartType::Text, text});
      req.messages.push_back(std::move(out));
      continue;
    }

    if (const auto* toolResult = std::get_if<ToolResultMessage>(&message)) {
      out.role = ProviderRole::Tool;
      // 配对 id 取 source (装载校验保证它与块内 toolCallId 一致; 恰好一个块)。
      out.toolCallId = toolResult->source.callId.value_or(CallId{}).value;
      for (const ToolResultBlock& block : toolResult->content) {
        for (const ToolResultContent& item : block.content) {
          if (const auto* text = std::get_if<TextBlock>(&item)) {
            out.parts.push_back(ProviderPart{PartType::Text, text->text});
          } else if (const auto* image = std::get_if<ImageBlock>(&item)) {
            out.parts.push_back(ProviderPart{
                PartType::ImageUrl, imageUrlOf(*image, config.attachmentStore)});
          }
          // 推理块不回灌: role:tool 的 wire 形态没有 reasoning 字段 (dsh 同)。
        }
      }
      req.messages.push_back(std::move(out));
      continue;
    }
  }
  return req;
}

// 从一行 SSE data 里尽力取 usage 与 finish_reason。
//
// 只在字符串确实包含对应键时才 parse: 逐 token 全量 parse 一遍在长回复里是可观的浪费。
struct StreamSideInfo {
  bool sawLengthFinish = false;
  std::optional<TokenUsage> usage;
};

void scanSideInfo(const std::string& dataLine, StreamSideInfo& info) {
  const bool hasFinish = dataLine.find("finish_reason") != std::string::npos;
  const bool hasUsage = dataLine.find("\"usage\"") != std::string::npos;
  if (!hasFinish && !hasUsage) return;

  Json chunk;
  try {
    chunk = parserJson(dataLine.c_str());
  } catch (...) {
    return;
  }
  if (!chunk.bObject()) return;

  if (hasFinish && chunk.find("choices") && chunk["choices"].bArray()
      && chunk["choices"].size() > 0) {
    const Json& choice = chunk["choices"].at(0);
    if (choice.bObject() && choice.find("finish_reason")
        && choice["finish_reason"].bString()
        && choice["finish_reason"].get<std::string>() == "length") {
      info.sawLengthFinish = true;
    }
  }

  if (hasUsage && chunk.find("usage") && chunk["usage"].bObject()) {
    const Json& usage = chunk["usage"];
    // dsh 记账词汇: inputTokens 只含未命中缓存的输入 —— OpenAI 兼容后端的
    // prompt_tokens 含缓存命中, 这里减掉; 细分字段有才写。
    TokenUsage counted;
    int64_t promptTokens = 0;
    int64_t cachedTokens = 0;
    if (usage.find("prompt_tokens") && usage["prompt_tokens"].bInt()) {
      promptTokens = usage["prompt_tokens"].get<int64_t>();
    }
    if (usage.find("completion_tokens") && usage["completion_tokens"].bInt()) {
      counted.outputTokens = usage["completion_tokens"].get<int64_t>();
    }
    if (usage.find("prompt_tokens_details")
        && usage["prompt_tokens_details"].bObject()) {
      const Json& details = usage["prompt_tokens_details"];
      if (details.find("cached_tokens") && details["cached_tokens"].bInt()) {
        cachedTokens = details["cached_tokens"].get<int64_t>();
      }
    }
    if (usage.find("completion_tokens_details")
        && usage["completion_tokens_details"].bObject()) {
      const Json& details = usage["completion_tokens_details"];
      if (details.find("reasoning_tokens")
          && details["reasoning_tokens"].bInt()) {
        counted.reasoningTokens = details["reasoning_tokens"].get<int64_t>();
      }
    }
    counted.inputTokens = promptTokens > cachedTokens
                              ? promptTokens - cachedTokens
                              : 0;
    if (cachedTokens > 0) counted.cacheReadTokens = cachedTokens;
    if (counted.inputTokens > 0 || counted.outputTokens > 0) {
      info.usage = counted;
    }
  }
}

}  // namespace

LlmProviderAdapter::LlmProviderAdapter(Config configuration)
    : config(std::move(configuration)) {
  if (config.apiUrl.empty()) throw std::runtime_error("LLM 配置缺少 apiUrl");
  if (config.estimateBytesPerToken < 1) {
    throw std::runtime_error("estimateBytesPerToken 必须是正整数");
  }
  provider = makeProvider(config.providerName);
  if (provider == nullptr) throw std::runtime_error("无法创建 provider");
  transport = std::make_unique<HttplibTransport>();
  transport->setTimeouts(config.connTimeoutSec, config.readTimeoutSec);

  std::string pathPart;
  splitUrl(config.apiUrl, baseUrl, pathPart);
  basePath = pathPart + (config.apiPath.empty() ? std::string("/chat/completions")
                                                : config.apiPath);
}

LlmProviderAdapter::~LlmProviderAdapter() = default;

PreparedLlmCall LlmProviderAdapter::prepareCall(const LlmCallConfig& proposed) {
  PreparedLlmCall prepared;
  prepared.config = proposed;
  if (prepared.config.model.empty()) prepared.config.model = config.model;
  if (prepared.config.provider.empty()) prepared.config.provider = config.providerName;

  // 物化默认值并标记来源: 标记让驱动在下一次请求前把它们摘掉重解, 而用户显式设的值保留。
  //
  // temperature 物化但不标记 —— dsh 的 adapterDefaults wire 只放得下
  // {reasoningEffort, maxTokens}, avox 因此约定「永远显式写 temperature」: 轮换模型时
  // temperature 保留 (与旧行为一致), maxTokens 重算。
  if (!prepared.config.temperature.has_value()) {
    prepared.config.temperature = config.temperature;
  }
  if (!prepared.config.maxTokens.has_value()) {
    prepared.config.maxTokens = config.maxTokens;
    prepared.adapterDefaults.maxTokens = true;
  }
  // reasoning_effort 走 dsh adapterDefaults wire 的 {reasoningEffort, maxTokens} 标记规则
  // (SessionCodec.cpp:728 注释) —— 物化时打标, requestProposal 在切路由前摘除重算。
  // 与 maxTokens 同档: 不是用户显式设的就重算, 用户/UI 选模型时显式设的跨路由保留。
  if (!prepared.config.reasoningEffort.has_value()) {
    prepared.config.reasoningEffort = config.reasoningEffort;
    prepared.adapterDefaults.reasoningEffort = true;
  }
  // contextWindow 不填: OpenAI 兼容后端不公布它, 压缩策略会用它自己的 fallback。
  return prepared;
}

LlmFinish LlmProviderAdapter::stream(const LlmRequest& request,
                                    const LlmStreamHandler& handler) {
  const ProviderRequest providerRequest = toProviderRequest(request, config);
  const std::string payload = provider->buildPayload(providerRequest);

  // 取消: AbortSignal 直驱 socket shutdown —— 解除阻塞 recv 的唯一可靠手段
  // (等首 token / 长 thinking 时 onChunk 不跑, 光靠它返 false 够不到中断)。
  // fd 跨线程: SocketHook 在 httplib 连接线程写, onAbort 在调 abort 的线程 shutdown,
  // 故用 atomic; exchange 取出即清 -1, 保证只 shutdown 一次。
  std::atomic<intptr_t> activeSock{-1};
  ScopedRegistration cancelSubscription;
  if (request.signal != nullptr) {
    cancelSubscription = ScopedRegistration(request.signal->onAbort([&activeSock]() {
      const intptr_t s = activeSock.exchange(-1);
      if (s != (intptr_t)-1) {
#ifdef _WIN32
        shutdown((SOCKET)s, SD_BOTH);
#else
        shutdown((int)s, SHUT_RDWR);
#endif
      }
    }));
  }

  std::string sseBuffer;
  // tool_call 的 index → id: ToolCallArgs 分片只带 index, 而流协议的分片按 id 配对。
  std::map<int, std::string> indexToId;
  StreamSideInfo sideInfo;
  auto lastProgress = std::chrono::steady_clock::now();
  bool budgetExceeded = false;
  const int budgetSec = config.reasoningTimeoutSec;

  // dsh 流协议的块索引分配: SSE delta 不带块索引, 这里按「首见定序」分配 ——
  // reasoning / text 各占一个块, 工具调用按 provider 的 toolIndex 各占一块。
  // 驱动侧 BlockAssembler 按首见顺序组装, 两边规则一致, block-end 的载荷就是权威块。
  BlockAssembler assembler;
  int nextBlockIndex = 0;
  int reasoningBlock = -1;
  int textBlock = -1;
  std::map<int, int> toolBlockOf;
  auto emit = [&](const StreamChunk& chunk) {
    assembler.push(chunk);
    handler.onChunk(chunk);
  };

  const HttpSseResult result = transport->postSse(
      baseUrl, basePath, payload, config.apiKey,
      [&activeSock](intptr_t socket) { activeSock.store(socket); },
      [&](const char* data, size_t len) -> bool {
        if (request.signal != nullptr && request.signal->aborted()) return false;
        // 本片是否含真进展 (正文或工具调用)。推理 token 不算 —— 预算据此判定卡死。
        bool progressed = false;
        sseBuffer.append(data, len);

        size_t pos = 0;
        while (pos < sseBuffer.size()) {
          const size_t lineEnd = sseBuffer.find('\n', pos);
          if (lineEnd == std::string::npos) break;
          std::string line = sseBuffer.substr(pos, lineEnd - pos);
          if (!line.empty() && line.back() == '\r') line.pop_back();
          pos = lineEnd + 1;
          if (line.size() < 6 || line.compare(0, 6, "data: ") != 0) continue;

          const std::string dataLine = line.substr(6);
          if (provider->isStreamEnd(dataLine)) continue;
          scanSideInfo(dataLine, sideInfo);

          std::vector<SseEvent> events;
          provider->onSseLine(dataLine, events);
          for (const SseEvent& event : events) {
            switch (event.kind) {
              case SseEventKind::Text: {
                if (event.text.empty()) break;
                if (textBlock < 0) {
                  textBlock = nextBlockIndex++;
                  emit(StreamBlockStart{textBlock, "text"});
                }
                emit(StreamTextDelta{textBlock, event.text});
                progressed = true;
                break;
              }
              case SseEventKind::Reasoning: {
                if (event.text.empty()) break;
                if (reasoningBlock < 0) {
                  reasoningBlock = nextBlockIndex++;
                  emit(StreamBlockStart{reasoningBlock, "reasoning"});
                }
                emit(StreamReasoningDelta{reasoningBlock, event.text});
                break;
              }
              case SseEventKind::ToolCallBegin: {
                std::string& id = indexToId[event.toolIndex];
                if (!event.toolId.empty()) id = event.toolId;
                // 流式未给 id 就按 index 合成: 后端要求 id 与 tool_call_id 一一对应,
                // 而它只要在本轮内唯一即可。
                if (id.empty()) id = "call_" + std::to_string(event.toolIndex);
                const auto inserted =
                    toolBlockOf.emplace(event.toolIndex, nextBlockIndex);
                if (inserted.second) {
                  ++nextBlockIndex;
                  emit(StreamBlockStart{inserted.first->second, "tool-call"});
                }
                emit(StreamToolCallDelta{inserted.first->second, CallId(id),
                                         event.toolName.empty()
                                             ? std::nullopt
                                             : std::optional<std::string>(
                                                   event.toolName),
                                         std::string()});
                progressed = true;
                break;
              }
              case SseEventKind::ToolCallArgs: {
                std::string& id = indexToId[event.toolIndex];
                if (id.empty()) id = "call_" + std::to_string(event.toolIndex);
                const auto inserted =
                    toolBlockOf.emplace(event.toolIndex, nextBlockIndex);
                if (inserted.second) {
                  ++nextBlockIndex;
                  emit(StreamBlockStart{inserted.first->second, "tool-call"});
                }
                emit(StreamToolCallDelta{inserted.first->second, CallId(id),
                                         std::nullopt, event.argsFrag});
                progressed = true;
                break;
              }
              default:
                break;
            }
          }
        }
        sseBuffer.erase(0, pos);

        if (progressed) lastProgress = std::chrono::steady_clock::now();
        if (budgetSec > 0) {
          const auto idle = std::chrono::duration_cast<std::chrono::seconds>(
                                std::chrono::steady_clock::now() - lastProgress)
                                .count();
          if (idle >= budgetSec) {
            budgetExceeded = true;
            return false;
          }
        }
        return true;
      });

  // postSse 已返回: fd 归还 httplib, 清掉免得退订前的迟到回调 shutdown 被复用的 fd。
  activeSock.store(-1);

  // 流末协议 (dsh 顺序): block-end 逐块定稿 -> usage -> finish。
  // 局部 assembler 与驱动侧同规则组装, 装配结果即 block-end 的权威载荷。
  // 出错/取消也已产生的部分照发 —— 那些是模型真实吐过的内容, 回放保真优先。
  const std::vector<ContentBlock>& assembled = assembler.blocks();
  for (size_t i = 0; i < assembled.size(); ++i) {
    handler.onChunk(StreamBlockEnd{static_cast<int>(i), assembled[i]});
  }

  LlmFinish finish;
  // 取消优先: 它是用户意图, 不该被同时发生的 HTTP 错误掩盖。
  if (request.signal != nullptr && request.signal->aborted()) {
    finish.kind = LlmFinishKind::Aborted;
    handler.onChunk(StreamFinish{FinishAborted{LlmFailure{"请求已取消", "ABORTED"}}});
    return finish;
  }
  if (budgetExceeded) {
    finish.kind = LlmFinishKind::Error;
    finish.failure = LlmFailure{
        "模型持续推理超过 " + std::to_string(budgetSec) + "s 无进展, 已中断",
        "REASONING_TIMEOUT"};
    handler.onChunk(StreamFinish{FinishError{*finish.failure}});
    return finish;
  }
  if (!result.gotResponse) {
    finish.kind = LlmFinishKind::Error;
    finish.failure =
        LlmFailure{"连接失败: " + result.errReason, "CONNECTION_FAILED"};
    handler.onChunk(StreamFinish{FinishError{*finish.failure}});
    return finish;
  }
  if (result.status != 200) {
    finish.kind = LlmFinishKind::Error;
    // 真实原因在响应体里 (OpenRouter/上游的 JSON error), 只报 "HTTP 400" 是哑谜。
    // 非 SSE 的错误 body 同样流经 onChunk 收进了 sseBuffer; 压成一行截前 200 字节。
    // 截断可能落在多字节序列中间、body 本身也可能带脏字节 —— patch 修成 U+FFFD。
    std::string snippet = sseBuffer;
    snippet.erase(std::remove(snippet.begin(), snippet.end(), '\r'), snippet.end());
    std::replace(snippet.begin(), snippet.end(), '\n', ' ');
    if (snippet.size() > 200) snippet.resize(200);
    snippet = patchInvalidUtf8(snippet);
    LlmFailure failure{"HTTP " + std::to_string(result.status),
                       codeOfStatus(result.status)};
    if (!snippet.empty()) failure.message += ": " + snippet;
    failure.status = result.status;
    finish.failure = failure;
    handler.onChunk(StreamFinish{FinishError{*finish.failure}});
    return finish;
  }

  finish.kind = sideInfo.sawLengthFinish ? LlmFinishKind::MaxTokens
                                        : LlmFinishKind::Completed;
  // usage 分片只记后端真实上报的记账 —— 字节估算不是流事实, 不进 chunk 流
  // (dsh 侧 token meter 若回放会把估算当真; 估算只落在 assistant/message 的 usage)。
  if (sideInfo.usage.has_value()) {
    finish.usage = sideInfo.usage;
    handler.onChunk(StreamUsage{*sideInfo.usage});
  } else {
    // 后端没报 usage: 按请求体字节数粗估 inputTokens。
    //
    // 压缩策略需要一个占用量判据, 没有它压缩永远不触发。阈值本身是 0.7 这种粗粒度,
    // 粗估足够; 真实 usage 一旦出现就优先。
    TokenUsage estimated;
    estimated.inputTokens =
        static_cast<int64_t>(payload.size() / config.estimateBytesPerToken);
    finish.usage = estimated;
  }

  // 结束分片镜像返回值 —— 二者由同一次分类产生, 控制流拿返回值, 日志拿分片, 不会分叉。
  FinishReason reason;
  if (finish.kind == LlmFinishKind::MaxTokens) {
    reason = FinishMaxTokens{};
  } else if (assembler.hasToolCalls()) {
    reason = FinishToolCalls{};
  } else {
    reason = FinishStop{};
  }
  handler.onChunk(StreamFinish{reason});
  return finish;
}

}
