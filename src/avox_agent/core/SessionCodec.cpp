#include "SessionCodec.hpp"

#include <stdexcept>
#include <utility>

#include "avox/module/Json.hpp"
#include "avox_agent/core/SessionTypes.hpp"
#include "avox/module/Utf8.hpp"

namespace avox {

namespace {

// ===========================================================================
// Json 安全读取
//
// 本项目 Json 的 const operator[] 对不存在的键会 assert, 所以每次取值都必须先 find。
// 这一组辅助把「缺字段 / 类型不符」统一变成一条带字段名的异常, 让坏日志的定位成本
// 接近于零。
// ===========================================================================

[[noreturn]] void bad(const char* field, const char* expected) {
  throw std::runtime_error(std::string("会话日志字段 \"") + field + "\" 缺失或不是"
                           + expected);
}

const Json& readMember(const Json& j, const char* key, const char* expected) {
  if (!j.bObject() || !j.find(key)) bad(key, expected);
  return j[key];
}

std::string readString(const Json& j, const char* key) {
  const Json& v = readMember(j, key, "字符串");
  if (!v.bString()) bad(key, "字符串");
  return v.get<std::string>();
}

int64_t readInt(const Json& j, const char* key) {
  const Json& v = readMember(j, key, "整数");
  if (!v.bInt()) bad(key, "整数");
  return v.get<int64_t>();
}

// JSON number 兼容整数与小数两种落盘形态 (dsh 的 temperature 可能是 0 也可能是 0.7)。
double readNumber(const Json& j, const char* key) {
  const Json& v = readMember(j, key, "数字");
  if (v.bInt()) return static_cast<double>(v.get<int64_t>());
  if (v.bNumber()) return v.get<double>();
  bad(key, "数字");
}

size_t readSeq(const Json& j, const char* key) {
  const int64_t value = readInt(j, key);
  if (value < 0) bad(key, "非负整数");
  return static_cast<size_t>(value);
}

bool readBool(const Json& j, const char* key) {
  const Json& v = readMember(j, key, "布尔");
  if (!v.bBool()) bad(key, "布尔");
  return v.get<bool>();
}

const Json& readObject(const Json& j, const char* key) {
  const Json& v = readMember(j, key, "对象");
  if (!v.bObject()) bad(key, "对象");
  return v;
}

const Json& readArray(const Json& j, const char* key) {
  const Json& v = readMember(j, key, "数组");
  if (!v.bArray()) bad(key, "数组");
  return v;
}

bool has(const Json& j, const char* key) { return j.bObject() && j.find(key); }

std::optional<std::string> readOptString(const Json& j, const char* key) {
  if (!has(j, key)) return std::nullopt;
  return readString(j, key);
}

std::optional<int64_t> readOptInt(const Json& j, const char* key) {
  if (!has(j, key)) return std::nullopt;
  return readInt(j, key);
}

// 只在有值时写入: 让「字段不存在」与「字段是空值」在 wire 上保持区分。
void writeOpt(Json& obj, const char* key, const std::optional<std::string>& v) {
  if (v.has_value()) obj[key] = *v;
}

void writeOptInt(Json& obj, const char* key, const std::optional<int64_t>& v) {
  if (v.has_value()) obj[key] = *v;
}

// ===========================================================================
// 枚举 <-> wire 字面量 (全部对齐 dsh)
// ===========================================================================

const char* sourceKindName(MessageSourceKind kind) {
  switch (kind) {
    case MessageSourceKind::User: return "user";
    case MessageSourceKind::Plugin: return "plugin";
    case MessageSourceKind::Model: return "model";
    case MessageSourceKind::Tool: return "tool";
    case MessageSourceKind::SkillInvocation: return "skill-invocation";
    case MessageSourceKind::TeamMessage: return "team-message";
  }
  return "user";
}

MessageSourceKind toSourceKind(const std::string& name) {
  if (name == "user") return MessageSourceKind::User;
  if (name == "plugin") return MessageSourceKind::Plugin;
  if (name == "model") return MessageSourceKind::Model;
  if (name == "tool") return MessageSourceKind::Tool;
  if (name == "skill-invocation") return MessageSourceKind::SkillInvocation;
  if (name == "team-message") return MessageSourceKind::TeamMessage;
  throw std::runtime_error("未知的消息来源类型: " + name
                           + " (dsh MessageSourceMap 之外的 kind, 需要移植语义)");
}

const char* headerReasonName(RequestHeaderReason reason) {
  switch (reason) {
    case RequestHeaderReason::Initial: return "initial";
    case RequestHeaderReason::Resume: return "resume";
    case RequestHeaderReason::Change: return "change";
  }
  return "initial";
}

RequestHeaderReason toHeaderReason(const std::string& name) {
  if (name == "initial") return RequestHeaderReason::Initial;
  if (name == "resume") return RequestHeaderReason::Resume;
  if (name == "change") return RequestHeaderReason::Change;
  throw std::runtime_error("未知的 request/header reason: " + name);
}

const char* inboxTargetName(InboxTarget target) {
  return target == InboxTarget::NextTurn ? "next-turn" : "next-step";
}

InboxTarget toInboxTarget(const std::string& name) {
  if (name == "next-turn") return InboxTarget::NextTurn;
  if (name == "next-step") return InboxTarget::NextStep;
  throw std::runtime_error("未知的 inbox target: " + name);
}

const char* approvalPolicyName(ApprovalPolicy policy) {
  return policy == ApprovalPolicy::Ask ? "ask" : "never";
}

ApprovalPolicy toApprovalPolicy(const std::string& name) {
  if (name == "ask") return ApprovalPolicy::Ask;
  if (name == "never") return ApprovalPolicy::Never;
  throw std::runtime_error("未知的审批策略: " + name);
}

const char* approvalOutcomeName(ApprovalOutcome outcome) {
  switch (outcome) {
    case ApprovalOutcome::AllowedOnce: return "allowed-once";
    case ApprovalOutcome::Rejected: return "rejected";
    case ApprovalOutcome::Cancelled: return "cancelled";
    case ApprovalOutcome::Unavailable: return "unavailable";
  }
  return "unavailable";
}

ApprovalOutcome toApprovalOutcome(const std::string& name) {
  if (name == "allowed-once") return ApprovalOutcome::AllowedOnce;
  if (name == "rejected") return ApprovalOutcome::Rejected;
  if (name == "cancelled") return ApprovalOutcome::Cancelled;
  if (name == "unavailable") return ApprovalOutcome::Unavailable;
  throw std::runtime_error("未知的审批结果: " + name);
}

const char* subagentModeName(SubagentMode mode) {
  return mode == SubagentMode::OneShot ? "one-shot" : "continuable";
}

SubagentMode toSubagentMode(const std::string& name) {
  if (name == "one-shot") return SubagentMode::OneShot;
  if (name == "continuable") return SubagentMode::Continuable;
  throw std::runtime_error(
      "未知的子代理模式: " + name + " (必须是 \"one-shot\" 或 \"continuable\")");
}

const char* todoStatusName(TodoStatus status) {
  switch (status) {
    case TodoStatus::Pending: return "pending";
    case TodoStatus::InProgress: return "in_progress";
    case TodoStatus::Completed: return "completed";
  }
  return "pending";
}

TodoStatus toTodoStatus(const std::string& name) {
  if (name == "pending") return TodoStatus::Pending;
  if (name == "in_progress") return TodoStatus::InProgress;
  if (name == "completed") return TodoStatus::Completed;
  throw std::runtime_error("未知的 todo 状态: " + name);
}

// ---- Agent Teams 枚举 (wire 字面量与 dsh agent-team 逐字一致) ----

const char* teamMemberPhaseName(TeamMemberPhase phase) {
  switch (phase) {
    case TeamMemberPhase::Provisioning: return "provisioning";
    case TeamMemberPhase::Active: return "active";
    case TeamMemberPhase::Failed: return "failed";
  }
  return "provisioning";
}

TeamMemberPhase toTeamMemberPhase(const std::string& name) {
  if (name == "provisioning") return TeamMemberPhase::Provisioning;
  if (name == "active") return TeamMemberPhase::Active;
  if (name == "failed") return TeamMemberPhase::Failed;
  throw std::runtime_error("未知的队友生命周期相位: " + name);
}

const char* teamMemberContextName(TeamMemberContext context) {
  return context == TeamMemberContext::Fresh ? "fresh" : "fork";
}

TeamMemberContext toTeamMemberContext(const std::string& name) {
  if (name == "fresh") return TeamMemberContext::Fresh;
  if (name == "fork") return TeamMemberContext::Fork;
  throw std::runtime_error("未知的队友上下文来源: " + name);
}

const char* teamTaskStatusName(TeamTaskStatus status) {
  switch (status) {
    case TeamTaskStatus::Pending: return "pending";
    case TeamTaskStatus::InProgress: return "in_progress";
    case TeamTaskStatus::Completed: return "completed";
    case TeamTaskStatus::Deleted: return "deleted";
  }
  return "pending";
}

TeamTaskStatus toTeamTaskStatus(const std::string& name) {
  if (name == "pending") return TeamTaskStatus::Pending;
  if (name == "in_progress") return TeamTaskStatus::InProgress;
  if (name == "completed") return TeamTaskStatus::Completed;
  if (name == "deleted") return TeamTaskStatus::Deleted;
  throw std::runtime_error("未知的共享任务状态: " + name);
}

const char* teamMessageDeliveryName(TeamMessageDelivery delivery) {
  return delivery == TeamMessageDelivery::Quiet ? "quiet" : "wakeup";
}

TeamMessageDelivery toTeamMessageDelivery(const std::string& name) {
  if (name == "quiet") return TeamMessageDelivery::Quiet;
  if (name == "wakeup") return TeamMessageDelivery::Wakeup;
  throw std::runtime_error("未知的队友消息投递模式: " + name);
}

// ===========================================================================
// 基础结构
// ===========================================================================

Json encode(const ImageAttachmentRef& ref) {
  Json obj(Json::JsonObject{});
  obj["attachmentId"] = ref.attachmentId;
  obj["mediaType"] = ref.mediaType;
  obj["bytes"] = ref.bytes;
  obj["width"] = ref.width;
  obj["height"] = ref.height;
  writeOpt(obj, "name", ref.name);
  return obj;
}

ImageAttachmentRef decodeAttachmentRef(const Json& j) {
  ImageAttachmentRef ref;
  ref.attachmentId = readString(j, "attachmentId");
  ref.mediaType = readString(j, "mediaType");
  ref.bytes = readInt(j, "bytes");
  ref.width = readInt(j, "width");
  ref.height = readInt(j, "height");
  ref.name = readOptString(j, "name");
  return ref;
}

Json encode(const ContentBlock& block) {
  Json obj(Json::JsonObject{});
  if (const auto* text = std::get_if<TextBlock>(&block)) {
    obj["type"] = "text";
    obj["text"] = text->text;
  } else if (const auto* reasoning = std::get_if<ReasoningBlock>(&block)) {
    obj["type"] = "reasoning";
    obj["text"] = reasoning->text;
  } else if (const auto* call = std::get_if<ToolCallBlock>(&block)) {
    obj["type"] = "tool-call";
    obj["id"] = call->id.value;
    obj["name"] = call->name;
    obj["arguments"] = call->arguments;
  } else if (const auto* image = std::get_if<ImageBlock>(&block)) {
    obj["type"] = "image";
    obj["attachment"] = encode(image->attachment);
  } else if (const auto* result = std::get_if<ToolResultBlock>(&block)) {
    obj["type"] = "tool-result";
    obj["toolCallId"] = result->toolCallId.value;
    Json content(Json::JsonArray{});
    for (const ToolResultContent& item : result->content) {
      Json inner(Json::JsonObject{});
      if (const auto* text = std::get_if<TextBlock>(&item)) {
        inner["type"] = "text";
        inner["text"] = text->text;
      } else if (const auto* reasoning = std::get_if<ReasoningBlock>(&item)) {
        inner["type"] = "reasoning";
        inner["text"] = reasoning->text;
      } else if (const auto* image = std::get_if<ImageBlock>(&item)) {
        inner["type"] = "image";
        inner["attachment"] = encode(image->attachment);
      }
      content.push_back(std::move(inner));
    }
    obj["content"] = std::move(content);
    if (result->isError) obj["isError"] = true;
  }
  return obj;
}

ContentBlock decodeContentBlock(const Json& j) {
  const std::string type = readString(j, "type");
  if (type == "text") return TextBlock{readString(j, "text")};
  if (type == "reasoning") return ReasoningBlock{readString(j, "text")};
  if (type == "tool-call") {
    return ToolCallBlock{CallId(readString(j, "id")), readString(j, "name"),
                         readString(j, "arguments")};
  }
  if (type == "image") {
    return ImageBlock{decodeAttachmentRef(readObject(j, "attachment"))};
  }
  if (type == "tool-result") {
    ToolResultBlock block;
    block.toolCallId = CallId(readString(j, "toolCallId"));
    const Json& arr = readArray(j, "content");
    block.content.reserve(arr.size());
    for (size_t i = 0; i < arr.size(); ++i) {
      const Json& item = arr.at(i);
      const std::string innerType = readString(item, "type");
      if (innerType == "text") {
        block.content.push_back(TextBlock{readString(item, "text")});
      } else if (innerType == "reasoning") {
        block.content.push_back(ReasoningBlock{readString(item, "text")});
      } else if (innerType == "image") {
        block.content.push_back(
            ImageBlock{decodeAttachmentRef(readObject(item, "attachment"))});
      } else {
        // tool-result 内出现嵌套工具块: dsh 类型上允许但无生产者 —— 遇到即说明有
        // 新语义需要移植, 响亮失败而不是静默丢块。
        throw std::runtime_error(
            "tool-result 内容块类型 \"" + innerType
            + "\" 不受支持 (嵌套工具块无既有生产者, 属需要移植语义的新形态)");
      }
    }
    if (has(j, "isError")) block.isError = readBool(j, "isError");
    return block;
  }
  throw std::runtime_error("未知的内容块类型: " + type);
}

Json encodeContent(const std::vector<ContentBlock>& content) {
  Json arr(Json::JsonArray{});
  for (const ContentBlock& block : content) arr.push_back(encode(block));
  return arr;
}

std::vector<ContentBlock> decodeContent(const Json& j, const char* key) {
  const Json& arr = readArray(j, key);
  std::vector<ContentBlock> content;
  content.reserve(arr.size());
  for (size_t i = 0; i < arr.size(); ++i) {
    content.push_back(decodeContentBlock(arr.at(i)));
  }
  return content;
}

Json encode(const MessageSource& source) {
  Json obj(Json::JsonObject{});
  obj["kind"] = sourceKindName(source.kind);
  // kind 条件字段: dsh 的 source 校验只看 kind (与 model 的 provider/model、tool 的
  // callId), 多余键虽不拒收, 但按 kind 精确写键让两边日志可逐字对照。
  switch (source.kind) {
    case MessageSourceKind::User:
      break;
    case MessageSourceKind::Plugin:
      obj["plugin"] = source.plugin.value_or("");
      writeOpt(obj, "form", source.form);
      // 压缩检查点扩展键 (dsh merge-extensible source)。
      writeOpt(obj, "compactionId", source.compactionId);
      writeOpt(obj, "sourceCommandId", source.sourceCommandId);
      break;
    case MessageSourceKind::Model:
      obj["provider"] = source.provider.value_or("");
      obj["model"] = source.model.value_or("");
      break;
    case MessageSourceKind::Tool:
      obj["callId"] = source.callId.value_or(CallId{}).value;
      break;
    case MessageSourceKind::SkillInvocation:
      obj["name"] = source.name.value_or("");
      obj["form"] = source.form.value_or(std::string("instructions"));
      break;
    case MessageSourceKind::TeamMessage:
      obj["teamId"] = source.teamId.value_or("");
      obj["messageId"] = source.messageId.value_or("");
      obj["senderId"] = source.senderId.value_or("");
      obj["senderName"] = source.senderName.value_or("");
      break;
  }
  return obj;
}

MessageSource decodeSource(const Json& j) {
  MessageSource source;
  source.kind = toSourceKind(readString(j, "kind"));
  switch (source.kind) {
    case MessageSourceKind::User:
      break;
    case MessageSourceKind::Plugin:
      source.plugin = readString(j, "plugin");
      source.form = readOptString(j, "form");
      source.compactionId = readOptString(j, "compactionId");
      source.sourceCommandId = readOptString(j, "sourceCommandId");
      break;
    case MessageSourceKind::Model:
      source.provider = readString(j, "provider");
      source.model = readString(j, "model");
      break;
    case MessageSourceKind::Tool:
      source.callId = CallId(readString(j, "callId"));
      break;
    case MessageSourceKind::SkillInvocation:
      source.name = readString(j, "name");
      source.form = readOptString(j, "form");
      break;
    case MessageSourceKind::TeamMessage:
      source.teamId = readString(j, "teamId");
      source.messageId = readString(j, "messageId");
      source.senderId = readString(j, "senderId");
      source.senderName = readString(j, "senderName");
      break;
  }
  return source;
}

Json encode(const UserMessage& message) {
  Json obj(Json::JsonObject{});
  obj["id"] = message.id.value;
  obj["role"] = "user";
  obj["content"] = encodeContent(message.content);
  obj["source"] = encode(message.source);
  return obj;
}

UserMessage decodeUserMessage(const Json& j) {
  UserMessage message;
  message.id = MessageId(readString(j, "id"));
  // dsh 装载校验: user/message 的 role 必须是 "user"。在解码层同步检查, 坏形状
  // 在读取现场暴露, 而不是等投影出了错再回头找。
  if (readString(j, "role") != "user") bad("role", "user");
  message.content = decodeContent(j, "content");
  message.source = decodeSource(readObject(j, "source"));
  return message;
}

Json encode(const AssistantMessage& message) {
  Json obj(Json::JsonObject{});
  obj["id"] = message.id.value;
  obj["role"] = "assistant";
  obj["content"] = encodeContent(message.content);
  obj["source"] = encode(message.source);
  return obj;
}

AssistantMessage decodeAssistantMessage(const Json& j) {
  AssistantMessage message;
  message.id = MessageId(readString(j, "id"));
  if (readString(j, "role") != "assistant") bad("role", "assistant");
  message.content = decodeContent(j, "content");
  message.source = decodeSource(readObject(j, "source"));
  return message;
}

Json encode(const ToolResultMessage& message) {
  Json obj(Json::JsonObject{});
  obj["id"] = message.id.value;
  // dsh 形状: 工具结果以 user 角色回灌模型历史。
  obj["role"] = "user";
  Json content(Json::JsonArray{});
  for (const ToolResultBlock& block : message.content) {
    content.push_back(encode(block));
  }
  obj["content"] = std::move(content);
  obj["source"] = encode(message.source);
  return obj;
}

ToolResultMessage decodeToolResultMessage(const Json& j) {
  ToolResultMessage message;
  message.id = MessageId(readString(j, "id"));
  if (readString(j, "role") != "user") bad("role", "user (tool result)");
  message.source = decodeSource(readObject(j, "source"));
  const Json& arr = readArray(j, "content");
  for (size_t i = 0; i < arr.size(); ++i) {
    const Json& item = arr.at(i);
    if (!item.bObject() || !has(item, "type")
        || readString(item, "type") != "tool-result") {
      throw std::runtime_error(
          "tool/result 消息的 content 必须只含 tool-result 块 (dsh 校验: 恰好一个)");
    }
    message.content.push_back(
        std::get<ToolResultBlock>(decodeContentBlock(item)));
  }
  if (message.content.size() != 1) {
    throw std::runtime_error("tool/result 消息必须恰好一个 tool-result 块, 实得 "
                             + std::to_string(message.content.size()));
  }
  // dsh 装载校验: 块内 toolCallId 必须与 source.callId 一致。
  const std::string& sourceCallId = message.source.callId.value_or(CallId{}).value;
  if (message.content.front().toolCallId.value != sourceCallId) {
    throw std::runtime_error("tool/result 的 toolCallId 与 source.callId 不一致");
  }
  return message;
}

// ===========================================================================
// 流分片 (StreamChunk 7 变体) 与 FinishReason
// ===========================================================================

Json encode(const TokenUsage& usage) {
  Json obj(Json::JsonObject{});
  obj["inputTokens"] = usage.inputTokens;
  obj["outputTokens"] = usage.outputTokens;
  writeOptInt(obj, "cacheReadTokens", usage.cacheReadTokens);
  writeOptInt(obj, "cacheWriteTokens", usage.cacheWriteTokens);
  writeOptInt(obj, "reasoningTokens", usage.reasoningTokens);
  return obj;
}

TokenUsage decodeUsage(const Json& j) {
  TokenUsage usage;
  usage.inputTokens = readInt(j, "inputTokens");
  usage.outputTokens = readInt(j, "outputTokens");
  usage.cacheReadTokens = readOptInt(j, "cacheReadTokens");
  usage.cacheWriteTokens = readOptInt(j, "cacheWriteTokens");
  usage.reasoningTokens = readOptInt(j, "reasoningTokens");
  return usage;
}

Json encode(const LlmFailure& failure) {
  Json obj(Json::JsonObject{});
  obj["message"] = failure.message;
  obj["code"] = failure.code;
  if (failure.status.has_value()) {
    obj["status"] = static_cast<int64_t>(*failure.status);
  }
  writeOptInt(obj, "providerRetryAfterMs", failure.providerRetryAfterMs);
  writeOpt(obj, "requestId", failure.requestId);
  return obj;
}

LlmFailure decodeFailure(const Json& j) {
  LlmFailure failure;
  failure.message = readString(j, "message");
  failure.code = readString(j, "code");
  if (has(j, "status")) failure.status = static_cast<int>(readInt(j, "status"));
  failure.providerRetryAfterMs = readOptInt(j, "providerRetryAfterMs");
  failure.requestId = readOptString(j, "requestId");
  return failure;
}

Json encode(const FinishReason& reason) {
  Json obj(Json::JsonObject{});
  if (std::holds_alternative<FinishStop>(reason)) {
    obj["kind"] = "stop";
  } else if (std::holds_alternative<FinishToolCalls>(reason)) {
    obj["kind"] = "tool-calls";
  } else if (std::holds_alternative<FinishMaxTokens>(reason)) {
    obj["kind"] = "max-tokens";
  } else if (const auto* aborted = std::get_if<FinishAborted>(&reason)) {
    obj["kind"] = "aborted";
    obj["failure"] = encode(aborted->failure);
  } else if (const auto* error = std::get_if<FinishError>(&reason)) {
    obj["kind"] = "error";
    obj["failure"] = encode(error->failure);
  }
  return obj;
}

FinishReason decodeFinishReason(const Json& j) {
  const std::string kind = readString(j, "kind");
  if (kind == "stop") return FinishStop{};
  if (kind == "tool-calls") return FinishToolCalls{};
  if (kind == "max-tokens") return FinishMaxTokens{};
  if (kind == "aborted") return FinishAborted{decodeFailure(readObject(j, "failure"))};
  if (kind == "error") return FinishError{decodeFailure(readObject(j, "failure"))};
  throw std::runtime_error("未知的流结束原因: " + kind);
}

Json encode(const StreamChunk& chunk) {
  Json obj(Json::JsonObject{});
  if (const auto* start = std::get_if<StreamBlockStart>(&chunk)) {
    obj["type"] = "block-start";
    obj["index"] = static_cast<int64_t>(start->index);
    obj["blockType"] = start->blockType;
  } else if (const auto* text = std::get_if<StreamTextDelta>(&chunk)) {
    obj["type"] = "text-delta";
    obj["index"] = static_cast<int64_t>(text->index);
    obj["text"] = text->text;
  } else if (const auto* reasoning = std::get_if<StreamReasoningDelta>(&chunk)) {
    obj["type"] = "reasoning-delta";
    obj["index"] = static_cast<int64_t>(reasoning->index);
    obj["text"] = reasoning->text;
  } else if (const auto* call = std::get_if<StreamToolCallDelta>(&chunk)) {
    obj["type"] = "tool-call-delta";
    obj["index"] = static_cast<int64_t>(call->index);
    obj["id"] = call->id.value;
    writeOpt(obj, "name", call->name);
    obj["argumentsDelta"] = call->argumentsDelta;
  } else if (const auto* end = std::get_if<StreamBlockEnd>(&chunk)) {
    obj["type"] = "block-end";
    obj["index"] = static_cast<int64_t>(end->index);
    obj["block"] = encode(end->block);
  } else if (const auto* usage = std::get_if<StreamUsage>(&chunk)) {
    obj["type"] = "usage";
    obj["usage"] = encode(usage->usage);
  } else if (const auto* finish = std::get_if<StreamFinish>(&chunk)) {
    obj["type"] = "finish";
    obj["reason"] = encode(finish->reason);
  }
  return obj;
}

StreamChunk decodeChunk(const Json& j) {
  const std::string type = readString(j, "type");
  // usage/finish 在 wire 上没有 index (dsh 形状, 只有块定位类分片才有), 必须先分流,
  // 否则读 dsh 真实日志里这两种分片会在 readInt 上炸。
  if (type == "usage") return StreamUsage{decodeUsage(readObject(j, "usage"))};
  if (type == "finish") {
    return StreamFinish{decodeFinishReason(readObject(j, "reason"))};
  }
  const int index = static_cast<int>(readInt(j, "index"));
  if (type == "block-start") {
    return StreamBlockStart{index, readString(j, "blockType")};
  }
  if (type == "text-delta") {
    return StreamTextDelta{index, readString(j, "text")};
  }
  if (type == "reasoning-delta") {
    return StreamReasoningDelta{index, readString(j, "text")};
  }
  if (type == "tool-call-delta") {
    StreamToolCallDelta delta;
    delta.index = index;
    delta.id = CallId(readString(j, "id"));
    delta.name = readOptString(j, "name");
    delta.argumentsDelta = readString(j, "argumentsDelta");
    return delta;
  }
  if (type == "block-end") {
    return StreamBlockEnd{index, decodeContentBlock(readObject(j, "block"))};
  }
  throw std::runtime_error("未知的流分片类型: " + type);
}

// ===========================================================================
// 请求配置
// ===========================================================================

Json encode(const LlmCallConfig& config) {
  Json obj(Json::JsonObject{});
  obj["provider"] = config.provider;
  obj["model"] = config.model;
  writeOpt(obj, "reasoningEffort", config.reasoningEffort);
  // temperature 直接落 number (dsh 形状)。Json 的 Number 序列化走 std::to_string,
  // 理论上受 LC_NUMERIC 影响 —— 但本进程全链路无 setlocale 调用 (已核对), C locale
  // 恒为 "C", 小数点恒为 '.'; 旧实现的 temperatureMilli 定点方案因 dsh wire 不认而移除。
  if (config.temperature.has_value()) {
    obj["temperature"] = static_cast<double>(*config.temperature);
  }
  if (config.maxTokens.has_value()) {
    obj["maxTokens"] = static_cast<int64_t>(*config.maxTokens);
  }
  if (config.stop.has_value()) {
    Json stop(Json::JsonArray{});
    for (const std::string& s : *config.stop) stop.push_back(s);
    obj["stop"] = std::move(stop);
  }
  return obj;
}

LlmCallConfig decodeConfig(const Json& j) {
  LlmCallConfig config;
  config.provider = readString(j, "provider");
  config.model = readString(j, "model");
  config.reasoningEffort = readOptString(j, "reasoningEffort");
  if (has(j, "temperature")) {
    config.temperature = static_cast<float>(readNumber(j, "temperature"));
  }
  if (has(j, "maxTokens")) {
    config.maxTokens = static_cast<int>(readInt(j, "maxTokens"));
  }
  if (has(j, "stop")) {
    const Json& arr = readArray(j, "stop");
    std::vector<std::string> stop;
    stop.reserve(arr.size());
    for (size_t i = 0; i < arr.size(); ++i) {
      const Json& item = arr.at(i);
      if (!item.bString()) bad("stop", "字符串数组");
      stop.push_back(item.get<std::string>());
    }
    config.stop = std::move(stop);
  }
  return config;
}

Json encode(const EpochHeader& header) {
  Json obj(Json::JsonObject{});
  obj["config"] = encode(header.config);
  if (header.adapterDefaults.has_value()) {
    Json defaults(Json::JsonObject{});
    // dsh 对 adapterDefaults 做 exact-key 校验 (只认 {reasoningEffort?, maxTokens?},
    // 值必须是 true), 故只写为 true 的键。
    if (header.adapterDefaults->reasoningEffort) defaults["reasoningEffort"] = true;
    if (header.adapterDefaults->maxTokens) defaults["maxTokens"] = true;
    obj["adapterDefaults"] = std::move(defaults);
  }
  writeOpt(obj, "system", header.system);
  // toolsJson 是已序列化的 schema 数组。原样嵌入而不是当字符串塞进去 ——
  // 否则日志里会出现一大坨转义, 既不可读也没法与 dsh 的 tools 数组对照。
  if (header.toolsJson.has_value()) {
    obj["tools"] = parserJson(header.toolsJson->c_str());
  }
  return obj;
}

EpochHeader decodeEpochHeader(const Json& j) {
  EpochHeader header;
  header.config = decodeConfig(readObject(j, "config"));
  if (has(j, "adapterDefaults")) {
    const Json& defaults = readObject(j, "adapterDefaults");
    LlmCallConfigAdapterDefaults marks;
    if (has(defaults, "reasoningEffort")) {
      marks.reasoningEffort = readBool(defaults, "reasoningEffort");
    }
    if (has(defaults, "maxTokens")) {
      marks.maxTokens = readBool(defaults, "maxTokens");
    }
    header.adapterDefaults = marks;
  }
  header.system = readOptString(j, "system");
  if (has(j, "tools")) header.toolsJson = j["tools"].dump();
  return header;
}

Json encode(const RequestContext& context) {
  Json obj(Json::JsonObject{});
  obj["provider"] = context.provider;
  obj["model"] = context.model;
  writeOptInt(obj, "contextWindow", context.contextWindow);
  return obj;
}

RequestContext decodeRequestContext(const Json& j) {
  RequestContext context;
  context.provider = readString(j, "provider");
  context.model = readString(j, "model");
  context.contextWindow = readOptInt(j, "contextWindow");
  return context;
}

Json encode(const AgentCancelCause& cause) {
  Json obj(Json::JsonObject{});
  if (std::holds_alternative<CancelByUser>(cause)) {
    obj["kind"] = "user";
  } else if (std::holds_alternative<CancelByParent>(cause)) {
    obj["kind"] = "parent";
  } else if (const auto* hook = std::get_if<CancelByHook>(&cause)) {
    obj["kind"] = "hook";
    obj["reason"] = hook->reason;
  } else if (std::holds_alternative<CancelByDisposed>(cause)) {
    obj["kind"] = "disposed";
  } else {
    obj["kind"] = "legacy";
  }
  return obj;
}

AgentCancelCause decodeCancelCause(const Json& j) {
  const std::string kind = readString(j, "kind");
  if (kind == "user") return CancelByUser{};
  if (kind == "parent") return CancelByParent{};
  if (kind == "hook") return CancelByHook{readString(j, "reason")};
  if (kind == "disposed") return CancelByDisposed{};
  // dsh 的宽容值: 老日志的取消原因不可考。avox 不写出, 只在读侧接纳。
  if (kind == "legacy") return CancelByLegacy{};
  throw std::runtime_error("未知的取消原因: " + kind);
}

Json encode(const TurnEndReason& reason) {
  Json obj(Json::JsonObject{});
  if (std::holds_alternative<TurnEndCompleted>(reason)) {
    obj["kind"] = "completed";
  } else if (const auto* aborted = std::get_if<TurnEndAborted>(&reason)) {
    obj["kind"] = "aborted";
    obj["reason"] = encode(aborted->reason);
  } else if (std::holds_alternative<TurnEndBlocked>(reason)) {
    obj["kind"] = "blocked";
  } else if (const auto* error = std::get_if<TurnEndError>(&reason)) {
    obj["kind"] = "error";
    obj["error"] = encode(error->error);
  } else if (std::holds_alternative<TurnEndMaxTokens>(reason)) {
    obj["kind"] = "max-tokens";
  } else {
    obj["kind"] = "interrupted";
  }
  return obj;
}

TurnEndReason decodeTurnEndReason(const Json& j) {
  const std::string kind = readString(j, "kind");
  if (kind == "completed") return TurnEndCompleted{};
  if (kind == "aborted") {
    return TurnEndAborted{decodeCancelCause(readObject(j, "reason"))};
  }
  if (kind == "blocked") return TurnEndBlocked{};
  if (kind == "error") {
    return TurnEndError{decodeFailure(readObject(j, "error"))};
  }
  if (kind == "max-tokens") return TurnEndMaxTokens{};
  if (kind == "interrupted") return TurnEndInterrupted{};
  throw std::runtime_error("未知的 turn 结束原因: " + kind);
}

// ===========================================================================
// surface 标记
// ===========================================================================

Json encode(const SurfaceOp& op) {
  if (std::holds_alternative<SurfaceAppend>(op)) return Json(std::string("append"));
  const auto& replace = std::get<SurfaceReplace>(op);
  Json obj(Json::JsonObject{});
  obj["op"] = "replace";
  obj["start"] = static_cast<int64_t>(replace.start);
  obj["end"] = static_cast<int64_t>(replace.end);
  return obj;
}

SurfaceOp decodeSurfaceOp(const Json& j) {
  if (j.bString()) {
    const std::string value = j.get<std::string>();
    if (value == "append") return SurfaceAppend{};
    throw std::runtime_error("未知的 surfaceOp 字面量: " + value);
  }
  if (!j.bObject()) throw std::runtime_error("surfaceOp 必须是字符串或对象");
  const std::string op = readString(j, "op");
  if (op != "replace") throw std::runtime_error("未知的 surfaceOp op: " + op);
  SurfaceReplace replace;
  replace.start = readSeq(j, "start");
  replace.end = readSeq(j, "end");
  return replace;
}

// ===========================================================================
// Agent Teams 快照 (wire 形状 = dsh agent-team 的 snapshot 接口)
// ===========================================================================

Json encode(const TeamMemberSnapshot& member) {
  Json obj(Json::JsonObject{});
  obj["id"] = member.id.value;
  obj["name"] = member.name;
  obj["description"] = member.description;
  obj["provider"] = member.provider;
  obj["context"] = teamMemberContextName(member.context);
  obj["phase"] = teamMemberPhaseName(member.phase);
  writeOpt(obj, "error", member.error);
  return obj;
}

TeamMemberSnapshot decodeTeamMemberSnapshot(const Json& j) {
  TeamMemberSnapshot member;
  member.id = SessionId(readString(j, "id"));
  member.name = readString(j, "name");
  member.description = readString(j, "description");
  member.provider = readString(j, "provider");
  member.context = toTeamMemberContext(readString(j, "context"));
  member.phase = toTeamMemberPhase(readString(j, "phase"));
  member.error = readOptString(j, "error");
  return member;
}

Json encode(const TeamTaskSnapshot& task) {
  Json obj(Json::JsonObject{});
  obj["id"] = task.id;
  obj["revision"] = static_cast<int64_t>(task.revision);
  obj["subject"] = task.subject;
  obj["description"] = task.description;
  obj["status"] = teamTaskStatusName(task.status);
  if (task.ownerId.has_value()) obj["ownerId"] = task.ownerId->value;
  Json blocked(Json::JsonArray{});
  for (const std::string& id : task.blockedBy) blocked.push_back(id);
  obj["blockedBy"] = std::move(blocked);
  Json scopes(Json::JsonArray{});
  for (const std::string& scope : task.writeScopes) scopes.push_back(scope);
  obj["writeScopes"] = std::move(scopes);
  return obj;
}

TeamTaskSnapshot decodeTeamTaskSnapshot(const Json& j) {
  TeamTaskSnapshot task;
  task.id = readString(j, "id");
  task.revision = static_cast<int>(readInt(j, "revision"));
  task.subject = readString(j, "subject");
  task.description = readString(j, "description");
  task.status = toTeamTaskStatus(readString(j, "status"));
  if (has(j, "ownerId")) task.ownerId = SessionId(readString(j, "ownerId"));
  const Json& blocked = readArray(j, "blockedBy");
  task.blockedBy.reserve(blocked.size());
  for (size_t i = 0; i < blocked.size(); ++i) {
    const Json& item = blocked.at(i);
    if (!item.bString()) bad("blockedBy", "字符串数组");
    task.blockedBy.push_back(item.get<std::string>());
  }
  const Json& scopes = readArray(j, "writeScopes");
  task.writeScopes.reserve(scopes.size());
  for (size_t i = 0; i < scopes.size(); ++i) {
    const Json& item = scopes.at(i);
    if (!item.bString()) bad("writeScopes", "字符串数组");
    task.writeScopes.push_back(item.get<std::string>());
  }
  return task;
}

Json encode(const TeamMessageSnapshot& message) {
  Json obj(Json::JsonObject{});
  obj["id"] = message.id;
  obj["senderId"] = message.senderId.value;
  obj["senderName"] = message.senderName;
  obj["targetId"] = message.targetId.value;
  obj["delivery"] = teamMessageDeliveryName(message.delivery);
  obj["content"] = encodeContent(message.content);
  return obj;
}

TeamMessageSnapshot decodeTeamMessageSnapshot(const Json& j) {
  TeamMessageSnapshot message;
  message.id = readString(j, "id");
  message.senderId = SessionId(readString(j, "senderId"));
  message.senderName = readString(j, "senderName");
  message.targetId = SessionId(readString(j, "targetId"));
  message.delivery = toTeamMessageDelivery(readString(j, "delivery"));
  message.content = decodeContent(j, "content");
  return message;
}

// team/* 事件的 version 字段: dsh 恒写 1, 其它版本响亮拒绝 (与描述符版本同一哲学)。
void checkTeamEventVersion(int64_t version) {
  if (version != TEAM_EVENT_VERSION) {
    throw std::runtime_error("team/* 事件版本 " + std::to_string(version)
                             + " 不受支持 (当前 "
                             + std::to_string(TEAM_EVENT_VERSION) + ")");
  }
}

// ===========================================================================
// 事件载荷
// ===========================================================================

Json encode(const ToolResultError& error) {
  Json obj(Json::JsonObject{});
  obj["name"] = error.name;
  obj["code"] = error.code;
  return obj;
}

ToolResultError decodeToolResultError(const Json& j) {
  ToolResultError error;
  error.name = readString(j, "name");
  error.code = readString(j, "code");
  return error;
}

// compaction 的 turn 字段: dsh 恒写该键, 无值时写 null (不是省略)。
void writeTurnNullable(Json& obj, const std::optional<int>& turn) {
  if (turn.has_value()) {
    obj["turn"] = static_cast<int64_t>(*turn);
  } else {
    obj["turn"] = Json();
  }
}

Json encodeData(const EventData& data) {
  Json obj(Json::JsonObject{});
  switch (eventTypeOf(data)) {
    case EventType::TurnStart:
      obj["turn"] = static_cast<int64_t>(std::get<TurnStartData>(data).turn);
      break;

    case EventType::TurnEnd: {
      const auto& d = std::get<TurnEndData>(data);
      obj["turn"] = static_cast<int64_t>(d.turn);
      obj["reason"] = encode(d.reason);
      break;
    }

    case EventType::StepStart: {
      const auto& d = std::get<StepStartData>(data);
      obj["turn"] = static_cast<int64_t>(d.turn);
      obj["step"] = static_cast<int64_t>(d.step);
      break;
    }

    case EventType::StepEnd: {
      const auto& d = std::get<StepEndData>(data);
      obj["turn"] = static_cast<int64_t>(d.turn);
      obj["step"] = static_cast<int64_t>(d.step);
      break;
    }

    case EventType::UserMessageEvent:
      // dsh 形状: user/message 的 data 就是消息本体。
      return encode(std::get<UserMessageData>(data).message);

    case EventType::AssistantChunk: {
      const auto& d = std::get<AssistantChunkData>(data);
      obj["turn"] = static_cast<int64_t>(d.turn);
      obj["step"] = static_cast<int64_t>(d.step);
      obj["chunk"] = encode(d.chunk);
      break;
    }

    case EventType::AssistantMessageEvent: {
      const auto& d = std::get<AssistantMessageData>(data);
      obj["turn"] = static_cast<int64_t>(d.turn);
      obj["step"] = static_cast<int64_t>(d.step);
      obj["message"] = encode(d.message);
      if (d.usage.has_value()) obj["usage"] = encode(*d.usage);
      if (d.interrupted) obj["interrupted"] = true;
      break;
    }

    case EventType::ToolCall: {
      const auto& d = std::get<ToolCallData>(data);
      obj["turn"] = static_cast<int64_t>(d.turn);
      obj["step"] = static_cast<int64_t>(d.step);
      obj["callId"] = d.callId.value;
      obj["name"] = d.name;
      obj["arguments"] = d.arguments;
      break;
    }

    case EventType::ToolResult: {
      const auto& d = std::get<ToolResultData>(data);
      obj["turn"] = static_cast<int64_t>(d.turn);
      obj["step"] = static_cast<int64_t>(d.step);
      obj["message"] = encode(d.message);
      if (d.error.has_value()) obj["error"] = encode(*d.error);
      if (d.meta.has_value()) obj["meta"] = parserJson(d.meta->c_str());
      break;
    }

    case EventType::RequestHeaderEvent: {
      const auto& d = std::get<RequestHeaderData>(data);
      obj["header"] = encode(d.header);
      obj["reason"] = headerReasonName(d.reason);
      break;
    }

    case EventType::RequestContextEvent:
      // dsh 形状: request/context 的 data 就是 context 本体。
      return encode(std::get<RequestContextData>(data).context);

    case EventType::InboxSpliced: {
      const auto& d = std::get<InboxSplicedData>(data);
      obj["target"] = inboxTargetName(d.target);
      obj["start"] = static_cast<int64_t>(d.start);
      if (d.removedCount.has_value()) {
        obj["removedCount"] = static_cast<int64_t>(*d.removedCount);
      }
      Json inserted(Json::JsonArray{});
      for (const UserMessage& message : d.inserted) {
        inserted.push_back(encode(message));
      }
      obj["inserted"] = std::move(inserted);
      // 与 dsh 一致: 只在确实是取消时才写 outcome, claim 的纯删除不写。
      writeOpt(obj, "outcome", d.outcome);
      break;
    }

    case EventType::SessionEndSeed:
      // 载荷为空 —— 位置与 time 承载全部含义。
      break;

    case EventType::CompactionStart: {
      const auto& d = std::get<CompactionStartData>(data);
      obj["compactionId"] = d.compactionId;
      writeOpt(obj, "sourceCommandId", d.sourceCommandId);
      writeTurnNullable(obj, d.turn);
      break;
    }

    case EventType::CompactionSummary: {
      const auto& d = std::get<CompactionSummaryData>(data);
      obj["compactionId"] = d.compactionId;
      writeOpt(obj, "sourceCommandId", d.sourceCommandId);
      obj["summary"] = encodeContent(d.summary);
      Json range(Json::JsonObject{});
      range["start"] = static_cast<int64_t>(d.shadowedStart);
      range["end"] = static_cast<int64_t>(d.shadowedEnd);
      obj["shadowedRange"] = std::move(range);
      Json shadowed(Json::JsonArray{});
      for (size_t seq : d.shadowedSeqs) {
        shadowed.push_back(Json(static_cast<int64_t>(seq)));
      }
      obj["shadowedSeqs"] = std::move(shadowed);
      obj["shadowedTokenCount"] = d.shadowedTokenCount;
      obj["provider"] = d.provider;
      obj["model"] = d.model;
      if (d.maxTokens.has_value()) {
        obj["maxTokens"] = static_cast<int64_t>(*d.maxTokens);
      }
      if (d.usage.has_value()) obj["usage"] = encode(*d.usage);
      break;
    }

    case EventType::CompactionEnd: {
      const auto& d = std::get<CompactionEndData>(data);
      obj["compactionId"] = d.compactionId;
      writeOpt(obj, "sourceCommandId", d.sourceCommandId);
      writeTurnNullable(obj, d.turn);
      writeOpt(obj, "error", d.error);
      break;
    }

    case EventType::ApprovalPolicyEvent: {
      const auto& d = std::get<ApprovalPolicyData>(data);
      obj["policy"] = approvalPolicyName(d.policy);
      writeOpt(obj, "source", d.source);
      break;
    }

    case EventType::ApprovalAsked: {
      const auto& d = std::get<ApprovalAskedData>(data);
      obj["id"] = d.id;
      obj["toolName"] = d.toolName;
      if (d.callId.has_value()) obj["callId"] = d.callId->value;
      writeOpt(obj, "reason", d.reason);
      break;
    }

    case EventType::ApprovalDecided: {
      const auto& d = std::get<ApprovalDecidedData>(data);
      obj["id"] = d.id;
      obj["outcome"] = approvalOutcomeName(d.outcome);
      break;
    }

    case EventType::TodoWrite: {
      // dsh wire: data 就是 {todos:[{content,status}]} 本体 (不包 message 键)。
      const auto& d = std::get<TodoWriteData>(data);
      Json todos(Json::JsonArray{});
      for (const TodoItem& item : d.todos) {
        Json entry(Json::JsonObject{});
        entry["content"] = item.content;
        entry["status"] = todoStatusName(item.status);
        todos.push_back(std::move(entry));
      }
      obj["todos"] = std::move(todos);
      break;
    }

    case EventType::SubagentDescriptor: {
      const auto& d = std::get<SubagentDescriptorData>(data);
      // dsh 读侧对描述符做 exact-key 校验 (one-shot 与 continuable 各有自己的键集),
      // 故按 mode 精确写键: one-shot 只写 {version,mode,provider,label?}。
      obj["version"] = static_cast<int64_t>(d.version);
      obj["mode"] = subagentModeName(d.mode);
      obj["provider"] = d.provider;
      if (d.mode == SubagentMode::Continuable) {
        // continuable 的 label 必填 (dsh 校验); 缺值写空串而非省略键。
        obj["label"] = d.label.value_or(std::string());
        writeOpt(obj, "agentProvider", d.agentProvider);
        writeOpt(obj, "agentModel", d.agentModel);
        writeOpt(obj, "persona", d.persona);
        if (d.toolFilter.has_value()) {
          Json filter(Json::JsonObject{});
          if (d.toolFilter->allow.has_value()) {
            Json allow(Json::JsonArray{});
            for (const std::string& name : *d.toolFilter->allow) allow.push_back(name);
            filter["allow"] = std::move(allow);
          }
          if (d.toolFilter->deny.has_value()) {
            Json deny(Json::JsonArray{});
            for (const std::string& name : *d.toolFilter->deny) deny.push_back(name);
            filter["deny"] = std::move(deny);
          }
          obj["toolFilter"] = std::move(filter);
        }
      } else {
        writeOpt(obj, "label", d.label);
      }
      break;
    }

    case EventType::TeamMember: {
      const auto& d = std::get<TeamMemberEventData>(data);
      obj["version"] = static_cast<int64_t>(d.version);
      obj["teamId"] = d.teamId.value;
      obj["member"] = encode(d.member);
      break;
    }

    case EventType::TeamTask: {
      const auto& d = std::get<TeamTaskEventData>(data);
      obj["version"] = static_cast<int64_t>(d.version);
      obj["teamId"] = d.teamId.value;
      obj["task"] = encode(d.task);
      break;
    }

    case EventType::TeamMessageQueued: {
      const auto& d = std::get<TeamMessageQueuedData>(data);
      obj["version"] = static_cast<int64_t>(d.version);
      obj["teamId"] = d.teamId.value;
      obj["message"] = encode(d.message);
      break;
    }

    case EventType::TeamMessageDelivered: {
      const auto& d = std::get<TeamMessageDeliveredData>(data);
      obj["version"] = static_cast<int64_t>(d.version);
      obj["teamId"] = d.teamId.value;
      obj["messageId"] = d.messageId;
      obj["targetId"] = d.targetId.value;
      break;
    }

    case EventType::Opaque:
      // 墓碑不进 encodeData: encodeEvent 直接回放原始 JSON 行。
      break;
  }
  return obj;
}

EventData decodeData(EventType type, const Json& j) {
  switch (type) {
    case EventType::TurnStart:
      return TurnStartData{static_cast<int>(readInt(j, "turn"))};

    case EventType::TurnEnd: {
      TurnEndData d;
      d.turn = static_cast<int>(readInt(j, "turn"));
      d.reason = decodeTurnEndReason(readObject(j, "reason"));
      return d;
    }

    case EventType::StepStart: {
      StepStartData d;
      d.turn = static_cast<int>(readInt(j, "turn"));
      d.step = static_cast<int>(readInt(j, "step"));
      return d;
    }

    case EventType::StepEnd: {
      StepEndData d;
      d.turn = static_cast<int>(readInt(j, "turn"));
      d.step = static_cast<int>(readInt(j, "step"));
      return d;
    }

    case EventType::UserMessageEvent:
      return UserMessageData{decodeUserMessage(j)};

    case EventType::AssistantChunk: {
      AssistantChunkData d;
      d.turn = static_cast<int>(readInt(j, "turn"));
      d.step = static_cast<int>(readInt(j, "step"));
      d.chunk = decodeChunk(readObject(j, "chunk"));
      return d;
    }

    case EventType::AssistantMessageEvent: {
      AssistantMessageData d;
      d.turn = static_cast<int>(readInt(j, "turn"));
      d.step = static_cast<int>(readInt(j, "step"));
      d.message = decodeAssistantMessage(readObject(j, "message"));
      if (has(j, "usage")) d.usage = decodeUsage(readObject(j, "usage"));
      if (has(j, "interrupted")) d.interrupted = readBool(j, "interrupted");
      return d;
    }

    case EventType::ToolCall: {
      ToolCallData d;
      d.turn = static_cast<int>(readInt(j, "turn"));
      d.step = static_cast<int>(readInt(j, "step"));
      d.callId = CallId(readString(j, "callId"));
      d.name = readString(j, "name");
      d.arguments = readString(j, "arguments");
      return d;
    }

    case EventType::ToolResult: {
      ToolResultData d;
      d.turn = static_cast<int>(readInt(j, "turn"));
      d.step = static_cast<int>(readInt(j, "step"));
      d.message = decodeToolResultMessage(readObject(j, "message"));
      if (has(j, "error")) d.error = decodeToolResultError(readObject(j, "error"));
      if (has(j, "meta")) d.meta = j["meta"].dump();
      return d;
    }

    case EventType::RequestHeaderEvent: {
      RequestHeaderData d;
      d.header = decodeEpochHeader(readObject(j, "header"));
      d.reason = toHeaderReason(readString(j, "reason"));
      return d;
    }

    case EventType::RequestContextEvent:
      return RequestContextData{decodeRequestContext(j)};

    case EventType::InboxSpliced: {
      InboxSplicedData d;
      d.target = toInboxTarget(readString(j, "target"));
      d.start = readSeq(j, "start");
      if (has(j, "removedCount")) d.removedCount = readSeq(j, "removedCount");
      const Json& inserted = readArray(j, "inserted");
      d.inserted.reserve(inserted.size());
      for (size_t i = 0; i < inserted.size(); ++i) {
        d.inserted.push_back(decodeUserMessage(inserted.at(i)));
      }
      d.outcome = readOptString(j, "outcome");
      return d;
    }

    case EventType::SessionEndSeed:
      return SessionEndSeedData{};

    case EventType::CompactionStart: {
      CompactionStartData d;
      d.compactionId = readString(j, "compactionId");
      d.sourceCommandId = readOptString(j, "sourceCommandId");
      // turn 键恒在: null 表示维护相位触发。
      if (has(j, "turn") && !j["turn"].bNull()) {
        d.turn = static_cast<int>(readInt(j, "turn"));
      }
      return d;
    }

    case EventType::CompactionSummary: {
      CompactionSummaryData d;
      d.compactionId = readString(j, "compactionId");
      d.sourceCommandId = readOptString(j, "sourceCommandId");
      d.summary = decodeContent(j, "summary");
      const Json& range = readObject(j, "shadowedRange");
      d.shadowedStart = readSeq(range, "start");
      d.shadowedEnd = readSeq(range, "end");
      const Json& seqs = readArray(j, "shadowedSeqs");
      d.shadowedSeqs.reserve(seqs.size());
      for (size_t i = 0; i < seqs.size(); ++i) {
        d.shadowedSeqs.push_back(readSeq(seqs.at(i), "shadowedSeqs[i]"));
      }
      d.shadowedTokenCount = readInt(j, "shadowedTokenCount");
      d.provider = readString(j, "provider");
      d.model = readString(j, "model");
      if (has(j, "maxTokens")) {
        d.maxTokens = static_cast<int>(readInt(j, "maxTokens"));
      }
      if (has(j, "usage")) d.usage = decodeUsage(readObject(j, "usage"));
      return d;
    }

    case EventType::CompactionEnd: {
      CompactionEndData d;
      d.compactionId = readString(j, "compactionId");
      d.sourceCommandId = readOptString(j, "sourceCommandId");
      if (has(j, "turn") && !j["turn"].bNull()) {
        d.turn = static_cast<int>(readInt(j, "turn"));
      }
      d.error = readOptString(j, "error");
      return d;
    }

    case EventType::ApprovalPolicyEvent: {
      ApprovalPolicyData d;
      d.policy = toApprovalPolicy(readString(j, "policy"));
      d.source = readOptString(j, "source");
      return d;
    }

    case EventType::ApprovalAsked: {
      ApprovalAskedData d;
      d.id = readString(j, "id");
      d.toolName = readString(j, "toolName");
      if (has(j, "callId")) d.callId = CallId(readString(j, "callId"));
      d.reason = readOptString(j, "reason");
      return d;
    }

    case EventType::ApprovalDecided: {
      ApprovalDecidedData d;
      d.id = readString(j, "id");
      d.outcome = toApprovalOutcome(readString(j, "outcome"));
      return d;
    }

    case EventType::TodoWrite: {
      TodoWriteData d;
      const Json& todos = readArray(j, "todos");
      d.todos.reserve(todos.size());
      for (size_t i = 0; i < todos.size(); ++i) {
        const Json& entry = todos.at(i);
        TodoItem item;
        item.content = readString(entry, "content");
        item.status = toTodoStatus(readString(entry, "status"));
        d.todos.push_back(std::move(item));
      }
      return d;
    }

    case EventType::SubagentDescriptor: {
      SubagentDescriptorData d;
      // 版本必须逐字等于当前值。dsh 侧 fold 对其它版本返回 undefined (无法分类但
      // 不拒日志); avox 选择响亮拒绝 —— 与 SESSION_FORMAT_VERSION 同一哲学: 未发布期
      // 不承诺兼容, 描述符版本 bump 即语义不兼容, 静默跳过会重建出错误分类的子会话。
      d.version = static_cast<int>(readInt(j, "version"));
      if (d.version != SUBAGENT_DESCRIPTOR_VERSION) {
        throw std::runtime_error(
            "subagent/descriptor 版本 " + std::to_string(d.version) + " 不受支持 (当前 "
            + std::to_string(SUBAGENT_DESCRIPTOR_VERSION) + ")");
      }
      d.mode = toSubagentMode(readString(j, "mode"));
      d.provider = readString(j, "provider");
      if (d.mode == SubagentMode::Continuable) {
        // continuable 的 label 必填 (dsh 校验); one-shot 可选。
        d.label = readString(j, "label");
        d.agentProvider = readOptString(j, "agentProvider");
        d.agentModel = readOptString(j, "agentModel");
        d.persona = readOptString(j, "persona");
        if (has(j, "toolFilter")) {
          const Json& filter = readObject(j, "toolFilter");
          SubagentToolFilter restriction;
          if (has(filter, "allow")) {
            const Json& arr = readArray(filter, "allow");
            std::vector<std::string> allow;
            allow.reserve(arr.size());
            for (size_t i = 0; i < arr.size(); ++i) {
              const Json& item = arr.at(i);
              if (!item.bString()) bad("toolFilter.allow", "字符串数组");
              allow.push_back(item.get<std::string>());
            }
            restriction.allow = std::move(allow);
          }
          if (has(filter, "deny")) {
            const Json& arr = readArray(filter, "deny");
            std::vector<std::string> deny;
            deny.reserve(arr.size());
            for (size_t i = 0; i < arr.size(); ++i) {
              const Json& item = arr.at(i);
              if (!item.bString()) bad("toolFilter.deny", "字符串数组");
              deny.push_back(item.get<std::string>());
            }
            restriction.deny = std::move(deny);
          }
          // dsh parseToolFilter: allow 与 deny 至少声明其一。
          if (!restriction.allow.has_value() && !restriction.deny.has_value()) {
            throw std::runtime_error(
                "subagent/descriptor 的 toolFilter 必须声明 allow 和/或 deny");
          }
          d.toolFilter = std::move(restriction);
        }
      } else {
        d.label = readOptString(j, "label");
      }
      return d;
    }

    case EventType::TeamMember: {
      TeamMemberEventData d;
      checkTeamEventVersion(readInt(j, "version"));
      d.teamId = SessionId(readString(j, "teamId"));
      d.member = decodeTeamMemberSnapshot(readObject(j, "member"));
      return d;
    }

    case EventType::TeamTask: {
      TeamTaskEventData d;
      checkTeamEventVersion(readInt(j, "version"));
      d.teamId = SessionId(readString(j, "teamId"));
      d.task = decodeTeamTaskSnapshot(readObject(j, "task"));
      return d;
    }

    case EventType::TeamMessageQueued: {
      TeamMessageQueuedData d;
      checkTeamEventVersion(readInt(j, "version"));
      d.teamId = SessionId(readString(j, "teamId"));
      d.message = decodeTeamMessageSnapshot(readObject(j, "message"));
      return d;
    }

    case EventType::TeamMessageDelivered: {
      TeamMessageDeliveredData d;
      checkTeamEventVersion(readInt(j, "version"));
      d.teamId = SessionId(readString(j, "teamId"));
      d.messageId = readString(j, "messageId");
      d.targetId = SessionId(readString(j, "targetId"));
      return d;
    }

    case EventType::Opaque:
      // 墓碑只在 decodeEvent 的白名单路径构造, 不经 decodeData。
      throw std::runtime_error("Opaque 事件没有可解码的载荷结构");
  }
  throw std::runtime_error(std::string("无法解码事件类型: ")
                           + eventTypeName(type));
}

}  // namespace

// ===========================================================================
// 顶层
// ===========================================================================

std::string encodeEvent(const SessionEvent& event) {
  // 墓碑: 原始行逐字节回放 —— 重写后的日志里 dsh 独有事件保持原样,
  // dsh 侧续跑时这些事件的语义不丢。
  if (const auto* opaque = std::get_if<OpaqueEventData>(&event.data)) {
    return opaque->json;
  }
  Json obj(Json::JsonObject{});
  obj["type"] = eventTypeName(event.type);
  obj["seq"] = static_cast<int64_t>(event.seq);
  // wire 名对齐 dsh 的 time; C++ 成员叫 timeMs 是为了让单位在代码里显式。
  obj["time"] = event.timeMs;
  obj["data"] = encodeData(event.data);
  if (event.surfaceOp.has_value()) obj["surfaceOp"] = encode(*event.surfaceOp);
  // 空集合落盘时省略 (与 dsh 的偏离)。
  //
  // dsh 区分「不带该字段」与「带一个空数组」, 后者表示「已知的空 provider 流」。这个区分
  // 对重建历史没有任何影响 —— 它只是一条元信息 —— 而本项目的 JSON 解析器不把 "[]" 识别
  // 成数组类型, 依赖它往返等于把持久格式的正确性押在一个第三方库的边缘行为上。
  // 内存里仍保留 optional (Session 的校验用得上: 只有 assistant/message 允许空集合)。
  if (event.sourceEventSeqs.has_value() && !event.sourceEventSeqs->empty()) {
    Json arr(Json::JsonArray{});
    for (size_t seq : *event.sourceEventSeqs) {
      arr.push_back(Json(static_cast<int64_t>(seq)));
    }
    obj["sourceEventSeqs"] = std::move(arr);
  }
  if (event.ignorable) obj["ignorable"] = true;
  // 落盘行必须是合法 UTF-8: 根目录里混进一条非 UTF-8 行, dsh 的 checkRootEncoding 会
  // 拒收整个 root。只修补不重编码 —— Json::dump 已转义全部结构性字符, 非法字节只可能
  // 在字符串字面量内, U+FFFD 修补不会碰坏 JSON 结构。
  return patchInvalidUtf8(obj.dump());
}

DecodedEvent decodeEvent(const std::string& line) {
  const Json j = parserJson(line.c_str());
  if (!j.bObject()) throw std::runtime_error("会话日志行不是 JSON 对象");

  const std::string typeName = readString(j, "type");
  const std::optional<EventType> type = fromEventTypeName(typeName);
  if (!type.has_value()) {
    // dsh 独有事件的白名单墓碑: 已确认与 surface 无关 (见 isDshLogOnlyEventTypeName
    // 的逐条注释), 原始行原样保留。
    if (isDshLogOnlyEventTypeName(typeName)) {
      DecodedEvent decoded;
      decoded.status = DecodedEvent::Status::Ok;
      decoded.event.type = EventType::Opaque;
      decoded.event.seq = readSeq(j, "seq");
      decoded.event.timeMs = readInt(j, "time");
      decoded.event.data = OpaqueEventData{typeName, line};
      return decoded;
    }
    // 未识别类型: 带 ignorable 才可跳过, 否则拒绝整个日志。
    const bool ignorable = has(j, "ignorable") && readBool(j, "ignorable");
    if (ignorable) return DecodedEvent{DecodedEvent::Status::SkippedIgnorable, {}};
    throw std::runtime_error(
        "未识别的会话事件类型 \"" + typeName
        + "\" 且未标记 ignorable: 拒绝重建会话 (一个未识别的必需事件可能改变其余"
          "日志的解释方式; 若它与 surface 无关, 应把它加进 dsh 白名单)");
  }

  DecodedEvent decoded;
  decoded.status = DecodedEvent::Status::Ok;
  SessionEvent& event = decoded.event;
  event.type = *type;
  event.seq = readSeq(j, "seq");
  event.timeMs = readInt(j, "time");
  event.data = decodeData(*type, readObject(j, "data"));
  if (has(j, "surfaceOp")) event.surfaceOp = decodeSurfaceOp(j["surfaceOp"]);
  if (has(j, "sourceEventSeqs")) {
    const Json& arr = readArray(j, "sourceEventSeqs");
    std::vector<size_t> seqs;
    seqs.reserve(arr.size());
    for (size_t i = 0; i < arr.size(); ++i) {
      const Json& item = arr.at(i);
      if (!item.bInt() || item.get<int64_t>() < 0) {
        throw std::runtime_error("sourceEventSeqs 必须是非负整数数组");
      }
      seqs.push_back(static_cast<size_t>(item.get<int64_t>()));
    }
    event.sourceEventSeqs = std::move(seqs);
  }
  event.ignorable = has(j, "ignorable") && readBool(j, "ignorable");
  return decoded;
}

std::string encodeHeader(const SessionHeader& header) {
  Json obj(Json::JsonObject{});
  // dsh 头行判别字段: 读者靠它把头行与事件行区分开。必须首键。
  obj["type"] = "session";
  obj["version"] = static_cast<int64_t>(header.version);
  obj["id"] = header.id.value;
  obj["createdAt"] = header.createdAt;
  writeOpt(obj, "cwd", header.cwd);
  if (header.parentSession.has_value()) {
    obj["parentSession"] = header.parentSession->value;
  }
  if (header.seedLength.has_value()) {
    obj["seedLength"] = static_cast<int64_t>(*header.seedLength);
  }
  writeOpt(obj, "origin", header.origin);
  // delegationDepth 恒写 (无值写 0): dsh 头行校验必填。
  obj["delegationDepth"] =
      static_cast<int64_t>(header.delegationDepth.value_or(0));
  writeOpt(obj, "agentPreset", header.agentPreset);
  // 同 encodeEvent: 头行同样保证合法 UTF-8 (cwd 等字段来自文件系统)。
  return patchInvalidUtf8(obj.dump());
}

SessionHeader decodeHeader(const std::string& line) {
  const Json j = parserJson(line.c_str());
  if (!j.bObject()) throw std::runtime_error("会话头不是 JSON 对象");
  // dsh 头行以 type:'session' 判别; 没有它说明不是 dsh 形状的日志。
  if (!has(j, "type") || readString(j, "type") != "session") {
    throw std::runtime_error(
        "会话头缺少 type:\"session\" 判别字段 (不是 dsh 形状的日志)");
  }
  // dsh 拒收已退役的策略基线字段 —— 同步拒收, 让坏日志在读取现场暴露。
  if (has(j, "sandboxMode") || has(j, "approvalPolicy")) {
    throw std::runtime_error("会话头使用了已退役的 sandboxMode/approvalPolicy 字段");
  }

  SessionHeader header;
  header.version = static_cast<int>(readInt(j, "version"));
  if (header.version != SESSION_FORMAT_VERSION) {
    throw std::runtime_error(
        "会话格式版本 " + std::to_string(header.version) + " 不受支持 (当前 "
        + std::to_string(SESSION_FORMAT_VERSION)
        + "); 未发布期不提供迁移, 直接拒绝");
  }
  header.id = SessionId(readString(j, "id"));
  header.createdAt = readInt(j, "createdAt");
  header.cwd = readOptString(j, "cwd");
  if (has(j, "parentSession")) {
    header.parentSession = SessionId(readString(j, "parentSession"));
  }
  if (has(j, "seedLength")) header.seedLength = readSeq(j, "seedLength");
  // delegationDepth 必填 (缺失即坏头行)。
  header.delegationDepth = static_cast<int>(readInt(j, "delegationDepth"));
  if (has(j, "origin")) {
    const std::string origin = readString(j, "origin");
    if (origin != "subagent") bad("origin", "'subagent'");
    header.origin = origin;
  }
  header.agentPreset = readOptString(j, "agentPreset");
  return header;
}

}
