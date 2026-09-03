// ChatProvider.cpp — OpenAI Chat Completions 协议 (payload 构建 + SSE 解析)
// 原样搬自 AgentClient 的 buildRequestPayload + onChunk delta 解析, 字节级复刻。
#include "ChatProvider.hpp"

#include <stdexcept>
#include <string>
#include <vector>

#include "avox/module/Json.hpp"

namespace avox {

const char* ChatProvider::name() { return "openai-chat"; }

// ========== ProviderPart → wire (复刻 partToJson) ==========

static Json partToWire(const ProviderPart& p) {
  Json j(Json::JsonObject{});
  if (p.type == PartType::Text) {
    j["type"] = "text";
    j["text"] = p.data;
  } else {
    j["type"] = "image_url";
    Json imgUrl(Json::JsonObject{});
    imgUrl["url"] = p.data;
    j["image_url"] = imgUrl;
  }
  return j;
}

// ========== ProviderMessage → wire (复刻 messageToJson + 扩展 assistant/tool) ==========

// tool 消息 wire (dsh #2724): role:tool 的 content 只收 string, 带不了图 —— 文本
// join 留下, 图片外提到 pendingImages (调用方在后续 user 消息里 flush)。空输出占位
// 与 dsh 逐字一致: 有图无文本 "(see attached image)", 全空 "(no output)"。
static Json toolToWire(const ProviderMessage& m, std::vector<Json>& pendingImages) {
  Json j(Json::JsonObject{});
  j["role"] = "tool";
  j["tool_call_id"] = m.toolCallId;
  bool hasImage = false;
  std::string text;
  for (const auto& p : m.parts) {
    if (p.type == PartType::Text) {
      text += p.data;
    } else {
      hasImage = true;
      pendingImages.push_back(partToWire(p));
    }
  }
  if (!text.empty()) j["content"] = text;
  else if (hasImage) j["content"] = "(see attached image)";
  else j["content"] = "(no output)";
  return j;
}

static Json messageToWire(const ProviderMessage& m) {
  Json j(Json::JsonObject{});
  switch (m.role) {
    case ProviderRole::System:    j["role"] = "system";    break;
    case ProviderRole::User:      j["role"] = "user";      break;
    case ProviderRole::Assistant: j["role"] = "assistant"; break;
    case ProviderRole::Tool:
      // tool 消息必须经 toolToWire (图片外提 + 占位文本), 走通用序列化会把图
      // 塞进 role:tool 的 content 数组 —— 后端直接拒。
      throw std::runtime_error("tool 消息须由 toolToWire 序列化");
  }
  // round-2 assistant(tool_calls): content="" + tool_calls=<预序列化 wire, dump→parse 还原保字节>
  if (m.role == ProviderRole::Assistant && !m.assistantToolCallsWire.empty()) {
    j["content"] = "";
    // CoT 回传 (dsh #2786): 带推理的轮必带, 键序与 dsh wire 一致 (content → reasoning_content → tool_calls)
    if (!m.assistantReasoning.empty()) j["reasoning_content"] = m.assistantReasoning;
    j["tool_calls"] = parserJson(m.assistantToolCallsWire.c_str());
    return j;
  }
  // content 形态 (dsh userContent): 含图 → 数组 (空文本 part 跳过, 保持 part 序);
  // 纯文本 → 全部 join 成单个 string (紧凑形态, 后端对数组容忍度不一)。
  bool hasImage = false;
  for (const auto& p : m.parts) {
    if (p.type != PartType::Text) hasImage = true;
  }
  if (!hasImage) {
    std::string text;
    for (const auto& p : m.parts) text += p.data;
    j["content"] = text;
  } else {
    Json content(Json::JsonArray{});
    for (const auto& p : m.parts) {
      if (p.type == PartType::Text && p.data.empty()) continue;
      content.push_back(partToWire(p));
    }
    j["content"] = content;
  }
  // CoT 回传 (dsh #2786): 纯文本 assistant 轮同样带 reasoning_content —— 后端忽略,
  // 网关把会话转码给别家时靠哈希这段文本恢复该轮的思维链签名。
  if (m.role == ProviderRole::Assistant && !m.assistantReasoning.empty()) {
    j["reasoning_content"] = m.assistantReasoning;
  }
  return j;
}

// ========== buildPayload (复刻 buildRequestPayload, 字节级一致) ==========

std::string ChatProvider::buildPayload(const ProviderRequest& req) {
  Json payload(Json::JsonObject{});
  payload["model"] = req.model.empty() ? std::string("local-model") : req.model;
  Json msgs(Json::JsonArray{});
  // system 注入 (会话内不变, 利 KV cache 缓存前缀)
  if (!req.systemPrompt.empty()) {
    Json sys(Json::JsonObject{});
    sys["role"] = "system";
    sys["content"] = req.systemPrompt;
    msgs.push_back(sys);
  }
  // routing 已由 Agent (applyRouting) 在 build*Request 时追加到 user.data, Provider 只序列化
  //
  // tool 图片 flush (dsh #2724): 连续 tool 结果的图片共享紧随其后的一条 user 消息
  // (前缀文本 + 图 parts), 遇 system/assistant/普通 user 消息或收尾时落地 ——
  // 保证 wire 序 [tool, tool, ..., user(图), 下一条消息]。
  std::vector<Json> pendingToolImages;
  auto flushToolImages = [&]() {
    if (pendingToolImages.empty()) return;
    Json j(Json::JsonObject{});
    j["role"] = "user";
    Json content(Json::JsonArray{});
    Json prefix(Json::JsonObject{});
    prefix["type"] = "text";
    prefix["text"] = "Attached image(s) from tool result:";
    content.push_back(prefix);
    for (Json& image : pendingToolImages) content.push_back(image);
    j["content"] = content;
    msgs.push_back(j);
    pendingToolImages.clear();
  };
  for (const auto& m : req.messages) {
    if (m.role == ProviderRole::Tool) {
      msgs.push_back(toolToWire(m, pendingToolImages));
      continue;
    }
    flushToolImages();
    msgs.push_back(messageToWire(m));
  }
  flushToolImages();
  payload["messages"] = msgs;
  payload["temperature"] = (double)req.temperature;
  payload["max_tokens"] = (int64_t)req.maxTokens;
  // 推理等级 (OpenAI reasoning_effort 兼容: 'low' / 'medium' / 'high')。空 = 不发, 留给非
  // 推理模型不踩雷; 默认值 "medium" 由 LlmProviderAdapter 在 toProviderRequest 里物化,
  // 切路由时通过 adapterDefaults 标记摘除重算。
  if (!req.reasoningEffort.empty()) {
    payload["reasoning_effort"] = req.reasoningEffort;
  }
  payload["stream"] = true;
  // dsh 恒发: DeepSeek 官方流式只有带 include_usage 才回 usage (否则只剩字节估算);
  // usage 行无 choices, onSseLine 自然跳过, 适配层 scanSideInfo 收账。OpenRouter 同支持。
  Json streamOptions(Json::JsonObject{});
  streamOptions["include_usage"] = true;
  payload["stream_options"] = streamOptions;
  // tools: 非空数组才挂 (首轮+round-2 同源)
  if (!req.toolsDefinitionJson.empty()) {
    Json tools = parserJson(req.toolsDefinitionJson.c_str());
    if (tools.bArray() && tools.size() > 0) payload["tools"] = tools;
  }
  return payload.dump();
}

// ========== onSseLine (复刻 onChunk delta 解析 → SseEvent) ==========

bool ChatProvider::onSseLine(const std::string& dataLine,
                             std::vector<SseEvent>& out) {
  Json chunk;
  try {
    chunk = parserJson(dataLine.c_str());
  } catch (...) {
    return false;
  }
  if (!chunk.bObject() || !chunk.find("choices") ||
      !chunk["choices"].bArray() || chunk["choices"].size() == 0) {
    return true;   // 非 choice 行 (如 ping/usage), 跳过
  }
  const Json& firstChoice = chunk["choices"][0];
  if (!firstChoice.bObject() || !firstChoice.find("delta")) return true;
  const Json& delta = firstChoice["delta"];
  if (!delta.bObject()) return true;
  // content → Text
  if (delta.find("content") && delta["content"].bString()) {
    SseEvent e;
    e.kind = SseEventKind::Text;
    e.text = delta["content"].get<std::string>();
    out.push_back(e);
  }
  // 推理流 (三键名首个命中, 只 emit 一个, R2): 不进 fullContent, 仅展示, 不计预算进展
  const char* reasoningKeys[] = {"reasoning_content", "reasoning", "thinking"};
  for (const char* key : reasoningKeys) {
    if (delta.find(key) && delta[key].bString()) {
      std::string token = delta[key].get<std::string>();
      if (!token.empty()) {
        SseEvent e;
        e.kind = SseEventKind::Reasoning;
        e.text = token;
        out.push_back(e);
      }
      break;
    }
  }
  // tool_calls → ToolCallBegin(若有 id/type/name) + ToolCallArgs(若有 arguments); Begin 在 Args 前 (R1)
  if (delta.find("tool_calls") && delta["tool_calls"].bArray()) {
    const Json& tcs = delta["tool_calls"];
    for (size_t k = 0; k < tcs.size(); k++) {
      const Json& tc = tcs[k];
      if (!tc.bObject() || !tc.find("index")) continue;
      int idx = (int)tc["index"].get<int64_t>();
      // Begin: 收集 id/type/name (非空才填, Agent 按非空累积)
      SseEvent begin;
      begin.kind = SseEventKind::ToolCallBegin;
      begin.toolIndex = idx;
      bool hasBegin = false;
      if (tc.find("id") && tc["id"].bString() && !tc["id"].get<std::string>().empty()) {
        begin.toolId = tc["id"].get<std::string>();
        hasBegin = true;
      }
      if (tc.find("type") && tc["type"].bString() && !tc["type"].get<std::string>().empty()) {
        begin.toolType = tc["type"].get<std::string>();
        hasBegin = true;
      }
      if (tc.find("function") && tc["function"].bObject()) {
        const Json& fn = tc["function"];
        if (fn.find("name") && fn["name"].bString() && !fn["name"].get<std::string>().empty()) {
          begin.toolName = fn["name"].get<std::string>();
          hasBegin = true;
        }
        if (fn.find("arguments") && fn["arguments"].bString()) {
          if (hasBegin) out.push_back(begin);   // Begin 在 Args 前
          SseEvent args;
          args.kind = SseEventKind::ToolCallArgs;
          args.toolIndex = idx;
          args.argsFrag = fn["arguments"].get<std::string>();
          out.push_back(args);
          continue;   // 已 push begin+args, 跳过尾部 push
        }
      }
      if (hasBegin) out.push_back(begin);
    }
  }
  return true;
}

bool ChatProvider::isStreamEnd(const std::string& dataLine) {
  return dataLine == "[DONE]";
}

}
