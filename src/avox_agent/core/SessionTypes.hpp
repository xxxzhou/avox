#pragma once

// ============================================================================
// 会话事件日志的类型词汇表。
//
// 对齐 deepseek-harness 的 packages/core/session/src/types.ts (以下简称 dsh)。
// 结构与命名有意 1:1 对应, 便于后期跟随 dsh 演进同步维护; 偏离处均在注释里标注。
//
// 本文件是纯类型声明: 不 include Json, 不 include 日志, 不依赖 avox_agent 其余部分。
// 事件 <-> JSON 的编解码在 SessionCodec.hpp, 文件读写在 SessionPersistence.hpp。
//
// 三条贯穿全局的约束 (破一条则其余设计连锁失效):
//   1. 日志是唯一真相。凡进入模型请求的内容, 必须能从本日志重建。
//   2. 模型历史是日志的投影, 不是日志本身。只有带 surfaceOp 的事件参与投影。
//   3. 压缩不删历史。压缩在投影层写一个 replace 节点遮蔽旧节点, 原事件永久保留。
//
// 2026-08 起与 dsh **wire 级互通**: avox 写出的日志 dsh 能装载续跑, dsh 写出的日志
// avox 能装载续跑。类型层因此直接采用 dsh 的概念划分 (reasoning 块、attachment 图片、
// tool-result 块、message source 的 model/tool 形状、inputTokens 记账……), 不留翻译层。
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "avox/AvoxDef.h"

namespace avox {

// ============================== 格式版本 ==============================

// 落盘会话格式版本, 盖在每个新写的 SessionHeader 上, 加载时逐一校验。
//
// 单调整数, 无 major/minor 拆分。是否需要 bump 由「写者产出什么」决定, 而不是由
// 「新读者能接受什么」决定: 当老运行时再也无法以完整语义正确性处理新日志时才 bump
// ——「能解析不报错」不等于正确, 静默跳过塑造重建的内容就是错读。只有结构性变化够得上
// 这条线: header 形状、SessionEvent 信封、核心事件语义、surface 机制 (哪些事件可上
// surface、SurfaceOp 有哪些变体)。
//
// 新增一个普通事件类型不 bump —— 词汇表增长由每事件的 ignorable 标记覆盖。
// 拿不准就 bump: 近似恒等的升级步骤几乎免费, 漏 bump 会让老运行时静默错读新日志。
//
// 未发布期钉在 0: 不承诺任何兼容性, 不兼容的日志直接拒绝, 不提供迁移。
inline constexpr int SESSION_FORMAT_VERSION = 0;

// ============================== 标识 ==============================

// 会话标识 (不透明跨边界 id, 故包一层而非裸 string)。
struct SessionId {
  std::string value;

  SessionId() = default;
  explicit SessionId(std::string v) : value(std::move(v)) {}
  bool empty() const { return value.empty(); }
  bool operator==(const SessionId& o) const { return value == o.value; }
  bool operator!=(const SessionId& o) const { return value != o.value; }
};

// 消息标识: inbox 用它做待处理消息的身份校验 (同一 id 不得同时 pending)。
// dsh 只要求「非空字符串」, avox 用确定性方案生成 (见 assistantMessageId /
// toolResultMessageId), 同一会话重放/续跑产生相同 id。
struct MessageId {
  std::string value;

  MessageId() = default;
  explicit MessageId(std::string v) : value(std::move(v)) {}
  bool empty() const { return value.empty(); }
  bool operator==(const MessageId& o) const { return value == o.value; }
};

// 工具调用标识: 把一次 tool/call 与它的 tool/result 配对。
// OpenAI 兼容后端严格要求 assistant.tool_calls[].id 与每条 role:tool 的
// tool_call_id 一一对应, 否则续发 400。
struct CallId {
  std::string value;

  CallId() = default;
  explicit CallId(std::string v) : value(std::move(v)) {}
  bool empty() const { return value.empty(); }
  bool operator==(const CallId& o) const { return value == o.value; }
};

// ============================== 存储元数据 ==============================

// 不可变的已校验存储元数据, 留在对话事件日志之外 (它是存储关切, 不是可重放的对话状态)。
//
// wire 键名与 dsh format.ts 的 HeaderLine 一致; delegationDepth 落盘恒写 (无值写 0)。
struct SessionHeader {
  // 落盘格式版本, 创建时盖 SESSION_FORMAT_VERSION。后端加载时遇到其它值直接拒绝。
  int version = SESSION_FORMAT_VERSION;
  SessionId id;
  // 创建时刻的 Unix epoch 毫秒。
  int64_t createdAt = 0;
  // 会话创建时的绝对工作目录 (存储后端按它归目录)。
  std::optional<std::string> cwd;
  // 本会话由哪个会话 fork 而来 (seed 血缘)。
  std::optional<SessionId> parentSession;
  // 经 seed 继承了多少条前导事件。持久化这条边界, 让 resume 与 replay 能区分
  // 父会话历史与子会话自己的工作。
  std::optional<size_t> seedLength;
  // 委派深度: 顶层 0, 子 agent 为父深度 + 1。落盘恒写 (dsh 头行校验必填)。
  std::optional<int> delegationDepth;
  // 'subagent': 子 agent 会话标记 (dsh 头行可选项); avox 不产生, 读入后忠实回写。
  std::optional<std::string> origin;
  // 本会话的 agent 由哪个 preset 组合而成 (按会话组合时)。
  // 之所以持久: preset 决定了会话的工具与提示词, 恢复成另一套组合会重放模型已无法
  // 依其行动的历史。
  std::optional<std::string> agentPreset;
};

// ============================== 消息与内容块 ==============================

// 图片附件引用 (dsh attachment 模型: 内容寻址存储, 消息里只放引用)。
//
// 字节本体不进日志 —— 存在附件仓 (见 adapter/DshAttachmentStore), 消息里只携带
// sha256 寻址与元数据。这样 dsh 与 avox 对图片消息的认知完全一致。
struct ImageAttachmentRef {
  // 附件标识, 形如 "sha256:<hex>"; 不是文件路径也不是 URL。
  std::string attachmentId;
  // image/png | image/jpeg | image/webp | image/gif。
  std::string mediaType;
  // 编码字节数。
  int64_t bytes = 0;
  // 编码内禀宽/高 (像素)。
  int64_t width = 0;
  int64_t height = 0;
  // 展示名 (不含本地路径信息); 可选。
  std::optional<std::string> name;
};

// 内容块的类型标签。切换时用 std::visit 或 index(), 新增变体必须更新所有 switch
// (见本文件末尾的 static_assert 闸门)。
struct TextBlock {
  std::string text;
};

// 推理流 (reasoning_content / thinking): 仅展示, 不进模型历史正文。wire 'reasoning'。
struct ReasoningBlock {
  std::string text;
};

// 模型请求的一次工具调用。arguments 是模型原样产出的 JSON 字符串 (未解析) ——
// 保留原样字节, 因为它要作为 assistant.tool_calls 原样回灌 wire。
struct ToolCallBlock {
  CallId id;
  std::string name;
  std::string arguments;
};

// 图片输入 (avox 是 VLM, 这是一等公民)。wire 'image', 载荷是附件引用。
struct ImageBlock {
  ImageAttachmentRef attachment;
};

// 一次已完成工具调用的结果块。tool/result 消息的 content **恰好一个** 本块
// (dsh 装载校验硬性要求), isError 与 toolCallId 都在块内 —— 这是 dsh 的
// tool-result 块形状, 与旧 avox 的「消息顶层 callId + isError」不同。
//
// content 的元素类型: dsh 形式上是 ContentBlock[], 但嵌套 tool-result/tool-call
// 无任何生产者 (工具结果只含 text/image); C++ variant 不允许不完整类型的递归,
// 故收窄为叶子块集合。解码遇到嵌套工具块时响亮失败 —— 那是需要移植语义的信号,
// 不是可以静默吞掉的形状差异。
using ToolResultContent = std::variant<TextBlock, ReasoningBlock, ImageBlock>;

struct ToolResultBlock {
  CallId toolCallId;
  std::vector<ToolResultContent> content;
  bool isError = false;
};

using ContentBlock =
    std::variant<TextBlock, ReasoningBlock, ToolCallBlock, ImageBlock,
                 ToolResultBlock>;

// ContentBlock -> ToolResultContent 的收窄: 工具产出的是完整内容块, 而工具结果消息的
// 块内只放叶子块。嵌套工具块没有生产者 —— 出现即是程序错误, 响亮失败。
inline ToolResultContent asToolResultContent(const ContentBlock& block) {
  if (const auto* text = std::get_if<TextBlock>(&block)) return *text;
  if (const auto* reasoning = std::get_if<ReasoningBlock>(&block)) {
    return *reasoning;
  }
  if (const auto* image = std::get_if<ImageBlock>(&block)) return *image;
  throw std::runtime_error(
      "工具结果内容里出现了工具块 (tool-call/tool-result 不能嵌套)");
}

// 反方向是全映射 (叶子块本就是 ContentBlock 的子集), 渲染与导出层用它取块文本。
inline ContentBlock asContentBlock(const ToolResultContent& item) {
  if (const auto* text = std::get_if<TextBlock>(&item)) return *text;
  if (const auto* reasoning = std::get_if<ReasoningBlock>(&item)) return *reasoning;
  return *std::get_if<ImageBlock>(&item);
}

// 一条消息的来源。dsh 的 MessageSourceMap 是 merge-extensible 的闭联类型:
//   {kind:'user'}
//   {kind:'plugin', plugin, form?}          — 插件合成注入 (含压缩摘要节点)
//   {kind:'model', provider, model}         — assistant 消息
//   {kind:'tool', callId}                   — tool/result 消息
//   {kind:'skill-invocation', name, form:'instructions'}  — dsh-skill 注入
//   {kind:'team-message', teamId, messageId, senderId, senderName}
//                                           — Agent Teams 的队友消息 (experimental 包)
// C++ 侧用一个扁平结构体承载 (字段按 kind 条件编解码), 避免多套小结构 +
// 二重 variant 的访问噪音。不发明 dsh 没有的 kind。
enum class MessageSourceKind {
  // 真人直接输入 (shell 里敲的那行)。
  User,
  // 插件合成注入 (agent.inject): 文件变更通知、运行时上下文、压缩摘要节点。
  Plugin,
  // assistant 消息: 产出它的路由。
  Model,
  // tool/result 消息: 关联的调用。
  Tool,
  // 用户显式手势加载的 skill 正文 (dsh-skill 的注入形态)。
  SkillInvocation,
  // Agent Teams 投递的队友消息 (dsh experimental agent-team 的 mailbox 专用)。
  // 目标会话靠它做持久去重: 同一 messageId 只投递一次。
  TeamMessage,
};

struct MessageSource {
  MessageSourceKind kind = MessageSourceKind::User;
  // kind=Plugin: 插件名 (dsh 的键名就是 plugin)。
  std::optional<std::string> plugin;
  // kind=SkillInvocation: skill 名。
  std::optional<std::string> name;
  // kind=Plugin/SkillInvocation: 注入形态 (dsh-skill 恒 'instructions')。
  std::optional<std::string> form;
  // kind=Model: 产出路由 (dsh 要求二者非空, 放 source 内而非消息顶层)。
  std::optional<std::string> provider;
  std::optional<std::string> model;
  // kind=Tool: 关联调用。
  std::optional<CallId> callId;
  // kind=TeamMessage: 投递方团队、消息、发送者 (dsh TeamMessageSource)。
  std::optional<std::string> teamId;
  std::optional<std::string> messageId;
  std::optional<std::string> senderId;
  std::optional<std::string> senderName;
  // kind=Plugin 的扩展键 (dsh 的 source 是 merge-extensible 的): 压缩检查点用
  // plugin:'compact' + compactionId (/sourceCommandId) 关联事务。别的扩展键
  // 读取时丢弃 —— 已知损失, 只影响诊断关联, 不影响语义。
  std::optional<std::string> compactionId;
  std::optional<std::string> sourceCommandId;
};

// 便捷构造 (kind 条件字段各就各位, 漏填在编码层会被 exact-key 纪律暴露)。
inline MessageSource userSource() { return MessageSource{}; }
inline MessageSource pluginSource(std::string pluginName,
                                  std::optional<std::string> injectForm = {}) {
  MessageSource s;
  s.kind = MessageSourceKind::Plugin;
  s.plugin = std::move(pluginName);
  s.form = std::move(injectForm);
  return s;
}
inline MessageSource modelSource(std::string providerName, std::string modelName) {
  MessageSource s;
  s.kind = MessageSourceKind::Model;
  s.provider = std::move(providerName);
  s.model = std::move(modelName);
  return s;
}
inline MessageSource toolSource(CallId callId) {
  MessageSource s;
  s.kind = MessageSourceKind::Tool;
  s.callId = std::move(callId);
  return s;
}
inline MessageSource skillInvocationSource(std::string skillName) {
  MessageSource s;
  s.kind = MessageSourceKind::SkillInvocation;
  s.name = std::move(skillName);
  s.form = std::string("instructions");
  return s;
}
// Agent Teams 队友消息的投递标记 (dsh mailbox): 目标会话按 messageId 去重,
// 按 senderName 展示来路。
inline MessageSource teamMessageSource(std::string teamId, std::string messageId,
                                       std::string senderId,
                                       std::string senderName) {
  MessageSource s;
  s.kind = MessageSourceKind::TeamMessage;
  s.teamId = std::move(teamId);
  s.messageId = std::move(messageId);
  s.senderId = std::move(senderId);
  s.senderName = std::move(senderName);
  return s;
}
// dsh 压缩检查点标记 (compaction/compaction/src/checkpoint.ts): 替换 user 消息的
// source 固定为 plugin:'compact' + compactionId (/sourceCommandId) 扩展键。
inline MessageSource compactCheckpointSource(std::string compactionId) {
  MessageSource s;
  s.kind = MessageSourceKind::Plugin;
  s.plugin = std::string("compact");
  s.compactionId = std::move(compactionId);
  return s;
}

// user 角色消息: 模型可见表面上的一条人类提示、合成上下文或目标续接。
// 三者的 content 都原样投影, 靠 source 区分。
struct UserMessage {
  MessageId id;
  std::vector<ContentBlock> content;
  MessageSource source;
};

// 一个 step 装配后的 assistant 消息 (派生历史用它, 不用 raw chunk)。
struct AssistantMessage {
  MessageId id;
  std::vector<ContentBlock> content;
  // 产出它的路由 (dsh 校验: kind='model' 且 provider/model 非空)。
  MessageSource source;
};

// 一次已完成工具调用的模型可见结果。
//
// wire 上 role 恒 'user' (dsh: tool result 以 user 角色回灌模型历史),
// content 恰好一个 ToolResultBlock, source = {kind:'tool', callId}。
struct ToolResultMessage {
  MessageId id;
  std::vector<ToolResultBlock> content;
  MessageSource source;
};

// 投影出的模型历史条目。
using Message = std::variant<UserMessage, AssistantMessage, ToolResultMessage>;

// 确定性消息 id: dsh 只要求非空, avox 让同一事件流重放产生相同 id,
// 便于跨进程 resume 时对齐。
inline std::string assistantMessageId(const SessionId& session, int turn,
                                      int step) {
  return session.value + "/assistant/" + std::to_string(turn) + "-"
         + std::to_string(step);
}
inline std::string toolResultMessageId(const SessionId& session, int turn,
                                       int step, const CallId& callId) {
  return session.value + "/tool-result/" + std::to_string(turn) + "-"
         + std::to_string(step) + "-" + callId.value;
}

// ============================== token 记账 ==============================

// dsh 的记账词汇: 字段间**不重叠**。inputTokens 只含未命中缓存的输入
// (DeepSeek 的 prompt_tokens 含缓存, 适配层负责减掉); outputTokens 是输出总量,
// reasoningTokens 是其中的推理细分 (不与之相加)。
struct TokenUsage {
  int64_t inputTokens = 0;
  int64_t outputTokens = 0;
  // 后端上报才写 (optional 落盘省略)。
  std::optional<int64_t> cacheReadTokens;
  std::optional<int64_t> cacheWriteTokens;
  std::optional<int64_t> reasoningTokens;
};

// ============================== 流分片 (raw stream chunk) ==============================
//
// dsh 适配层到驱动层的原始流协议: 7 个变体, 块索引把交错的 delta 关联到块,
// block-end 携带装配完成的块。assistant/chunk 事件逐分片落日志 (token 级回放保真)。
//
// wire 键: block-start{index,blockType} / text-delta{index,text} /
// reasoning-delta{index,text} / tool-call-delta{index,id,name?,argumentsDelta} /
// block-end{index,block} / usage{usage} / finish{reason}。

// 一次模型请求失败的可序列化事实 (在最终 adapter 边界规范化)。
// 定义在 StreamChunk 之前: FinishAborted/FinishError 按值持有它。
struct LlmFailure {
  std::string message;
  // 内部路由码 (如 CONTEXT_OVERFLOW / RATE_LIMITED / UNKNOWN)。
  std::string code;
  // 提供方 HTTP 状态码 (有才有)。
  std::optional<int> status;
  // 提供方要求的重试延迟 (毫秒, 有效且已知才有)。
  std::optional<int64_t> providerRetryAfterMs;
  // 提供方请求标识 (诊断用)。
  std::optional<std::string> requestId;
};

struct StreamBlockStart {
  int index = 0;
  // 'text' | 'reasoning' | 'tool-call' | 'image' | 'tool-result'。
  std::string blockType;
};

struct StreamTextDelta {
  int index = 0;
  std::string text;
};

struct StreamReasoningDelta {
  int index = 0;
  std::string text;
};

struct StreamToolCallDelta {
  int index = 0;
  CallId id;
  // 首片带名, 后续片省略。
  std::optional<std::string> name;
  std::string argumentsDelta;
};

struct StreamBlockEnd {
  int index = 0;
  ContentBlock block;
};

struct StreamUsage {
  TokenUsage usage;
};

struct FinishStop {};
struct FinishToolCalls {};
struct FinishMaxTokens {};
struct FinishAborted {
  LlmFailure failure;
};
struct FinishError {
  LlmFailure failure;
};

using FinishReason =
    std::variant<FinishStop, FinishToolCalls, FinishMaxTokens, FinishAborted,
                 FinishError>;

struct StreamFinish {
  FinishReason reason;
};

using StreamChunk =
    std::variant<StreamBlockStart, StreamTextDelta, StreamReasoningDelta,
                 StreamToolCallDelta, StreamBlockEnd, StreamUsage, StreamFinish>;

// ============================== 请求 header ==============================

// 一次调用的模型配置 (dsh LlmCallConfig)。temperature 落盘用 locale 无关的
// std::to_chars, 故直接存 float, 不再用 milli 定点。
struct LlmCallConfig {
  std::string provider;
  std::string model;
  // 'low' | 'medium' | 'high' (dsh ReasoningEffortId)。
  std::optional<std::string> reasoningEffort;
  std::optional<float> temperature;
  std::optional<int> maxTokens;
  std::optional<std::vector<std::string>> stop;
};

// 哪些 config 字段是由确切 adapter 物化的, 而非调用方提议的。
//
// 这个标记的用途: 把折叠出的 header 喂给下一次请求钩子之前, 先删掉被标记的字段, 于是
// 切换路由后新 adapter 会重新物化自己的默认值, 而用户显式设的值跨 step、跨路由保留。
// (avox 的 auto 免费模型轮换直接吃这条: 轮到另一个模型时 maxTokens 该重算。)
//
// dsh 的 wire 形状**恰好只允许** {reasoningEffort?, maxTokens?} 两个键 —— temperature
// 的物化标志放不下。avox 因此改为永远显式写 temperature (prepareCall 物化默认值但不
// 标记), 语义不受损: 轮换模型时 temperature 保留, 与旧行为一致。
struct LlmCallConfigAdapterDefaults {
  bool reasoningEffort = false;
  bool maxTokens = false;
};

// 请求中「非派生」部分的持久锚点: 配置 + system + 工具 schema。
//
// 「header 不变」等价于「请求前缀字节相同」, 这就是 KV cache 命中的判据, 也是一条
// 可断言的结构性质: 一次不动 prompt 的正常会话, request/header 事件应当只有一条。
//
// toolsJson 存 OpenAI function calling 数组的 JSON 文本; dsh wire 上 tools 是数组
// 本体 (ToolSchema[]), 编解码层做 string <-> array 的往返 (同库解析/序列化, 稳定)。
struct EpochHeader {
  LlmCallConfig config;
  std::optional<LlmCallConfigAdapterDefaults> adapterDefaults;
  // 渲染后的完整 system 提示词文本; 无 system 的请求则不填。
  //
  // 存渲染后的完整文本而非 section 列表 —— 外部语言注册的 section 在回放环境里根本
  // 不存在, 存列表就重建不出来。
  std::optional<std::string> system;
  // 装配后的工具 schema JSON (OpenAI function calling 数组); 无工具则不填。
  std::optional<std::string> toolsJson;
};

// 为何追加了一条 request/header 快照。
enum class RequestHeaderReason {
  // 日志的首条 header (新对话)。
  Initial,
  // 某个 loop 实例在一个已有 header 事件的日志上发的首个请求 (进程重启、fork seed)。
  Resume,
  // 后续某次请求用了不同的 header。
  Change,
};

// 一条已解析模型路由的注册期元数据。
struct RequestContext {
  std::string provider;
  std::string model;
  // 请求与响应合计的上下文上限 (后端公布时才有)。
  std::optional<int64_t> contextWindow;
};

// ============================== 取消与结束原因 ==============================

struct CancelByUser {};
struct CancelByParent {};
struct CancelByHook {
  std::string reason;
};
struct CancelByDisposed {};
// dsh 读侧宽容值: 老日志里 {kind:'legacy'} 的取消原因。avox 不产生, 只在解码时接纳。
struct CancelByLegacy {};

// 取消意图: 由活动的取消令牌携带, 第一个原因胜出。
using AgentCancelCause =
    std::variant<CancelByUser, CancelByParent, CancelByHook, CancelByDisposed,
                 CancelByLegacy>;

struct TurnEndCompleted {};
struct TurnEndAborted {
  // dsh 字段名是 reason (旧 avox 叫 cause)。
  AgentCancelCause reason;
};
// pre-step 拒绝: turn 关闭且不花一次模型调用。
struct TurnEndBlocked {};
struct TurnEndError {
  LlmFailure error;
};
struct TurnEndMaxTokens {};
// 崩溃遗留: 只由持久化层在 resume 时给未闭合的 turn 补写, 驱动自己永不产生。
// 于是一份日志里它的出现次数就等于崩溃次数, 是免费的可靠性指标。
struct TurnEndInterrupted {};

using TurnEndReason =
    std::variant<TurnEndCompleted, TurnEndAborted, TurnEndBlocked,
                 TurnEndError, TurnEndMaxTokens, TurnEndInterrupted>;

// ============================== surface 标记 ==============================

// 追加到模型可见表面的尾部 —— user/assistant/tool 消息的常规路径。
struct SurfaceAppend {};

// 用本节点替换 surface 上 [start, end] 区间内的既有节点 (两端均含)。
//
// start 与 end 是 **事件 seq**, 不是节点下标 —— 节点下标会随每次 replace 漂移,
// seq 永久稳定。两者都必须是当前 surface 上存在的节点; start == end 替换单个节点。
// 本事件的 sourceEventSeqs 必须覆盖每一个被遮蔽的节点。
//
// 压缩用它; 任何替换 surface 的产出方都可以用。
struct SurfaceReplace {
  size_t start = 0;
  size_t end = 0;
};

using SurfaceOp = std::variant<SurfaceAppend, SurfaceReplace>;

// append 时随事件提交的 surface 落位与来源引用。
// 产出消息的事件必须带, 仅记日志的事件禁止带。
struct SurfaceIntent {
  SurfaceOp surfaceOp;
  // 已知来源事件 seq 的完整集合。
  //
  // 「不带」与「带一个空集合」在内存里是两种语义, 故用 optional 而非空 vector 表达:
  // 不带 = 本事件不记录是哪些更早事件产生了它; assistant/message 可以带一个显式空集合
  // (表示已知的空 provider 流); 其它 surface 事件带此字段时必须非空 —— 这条校验规则需要
  // 这个区分。
  //
  // 但**落盘时空集合被省略** (见 SessionCodec.cpp 的说明): 那个区分对重建历史没有影响,
  // 不值得押在 JSON 库对 "[]" 的处理上。于是 resume 出来的空集合会变成「不带」, 而两者
  // 对投影与校验的结果相同。
  std::optional<std::vector<size_t>> sourceEventSeqs;
};

// ============================== 事件词汇表 ==============================
//
// 与 dsh SessionEventMap 逐条对应。新增普通事件类型不 bump 格式版本 —— 用 ignorable
// 标记覆盖词汇表增长。avox 自己产出的 21 种 + team/* 4 种: 前者在 dsh 的 KNOWN 集
// (46 种) 内; team/* 4 种来自 dsh experimental agent-team 的 SessionEventMap 合并声明,
// 装了该包的 dsh 认识, 未装的 dsh 拒读含 team/* 的 Lead 日志 —— 语义正确 (不懂队伍的
// 运行时本就不该续跑 Lead)。dsh 独有的事件读取时进 OpaqueEvent 墓碑 (见文件末尾)。
//
// 阶段划分: turn/step/user/assistant/tool/request/agent-inbox/end-seed/compaction/
// approval/todo/subagent-descriptor 已在此; hook/* 等读取时走墓碑, 不移植语义。

// 在 loop claim 排队输入与跑 pre-step 之前打开 turn。
// 拒绝、空输入、取消或失败都可能让它在不花 step 的情况下关闭。
struct TurnStartData {
  int turn = 0;
};

// 以结束它的原因关闭 turn。没有进入过 step 的 turn 没有 step/start 与 step/end。
struct TurnEndData {
  int turn = 0;
  TurnEndReason reason;
};

// 打开 turn 的第 step 步 —— 一次模型调用加上它请求的工具执行。
struct StepStartData {
  int turn = 0;
  int step = 0;
};

struct StepEndData {
  int turn = 0;
  int step = 0;
};

// 模型可见表面上的一条 user 消息。wire 上 data 就是消息本体 (不包 message 键)。
struct UserMessageData {
  UserMessage message;
};

// 原始流分片 —— token 级回放保真。仅记日志, 不进派生历史。
struct AssistantChunkData {
  int turn = 0;
  int step = 0;
  // dsh StreamChunk 7 变体之一 (含 block-end/usage/finish)。
  StreamChunk chunk;
};

// 一个 step 装配后的 assistant 消息 (派生历史用它)。
// adapter 报了 token 记账时随带本 step 的 usage —— 模型输出与它的记账一起走,
// 没有独立的 usage 记录。
struct AssistantMessageData {
  int turn = 0;
  int step = 0;
  AssistantMessage message;
  std::optional<TokenUsage> usage;
  // 取消定稿标记 (dsh #2134): 流消费期间被用户取消时, 已送达前缀仍收尾成
  // assistant/message —— 追问与分支要包含用户已读到的内容。仅取消路径置 true;
  // 失败的 attempt 永不收尾 (wire: interrupted?: true, 缺省即正常完成)。
  bool interrupted = false;
};

// 模型请求了一次工具调用。
struct ToolCallData {
  int turn = 0;
  int step = 0;
  CallId callId;
  std::string name;
  // 模型原样产出的 arguments JSON 字符串 (未解析)。
  std::string arguments;
};

// 工具结果的内部失败身份。与 LlmFailure ({message,code}) 不同形 —— dsh 的
// tool/result error 恰是 {name, code}: name 是工具名, 不是人类可读消息。
struct ToolResultError {
  std::string name;
  std::string code;
};

// 一次已完成工具调用的模型可见结果、可选的内部失败身份、可选的工具私有展示载荷。
//
// meta 对 core 不透明 (由产出工具拥有, 并在 presentResult 里读回), 但必须是合法 JSON:
// 它是 UI 卡片在回放时重现的唯一依据。
struct ToolResultData {
  int turn = 0;
  int step = 0;
  ToolResultMessage message;
  // 内部错误身份 (name + code), 与模型可见文本分离。
  std::optional<ToolResultError> error;
  // 工具私有展示载荷的 JSON 文本。
  std::optional<std::string> meta;
};

// 下一次请求的完整 header, 在它的 step 内、派发之前追加。
// 仅记日志; 最新的一条快照即可重建请求 header。
struct RequestHeaderData {
  EpochHeader header;
  RequestHeaderReason reason = RequestHeaderReason::Initial;
};

// 下一次请求的路由元数据, 仅在路由或容量变化时记。wire 上 data 就是 context 本体。
// 不参与请求重建, 也不参与 header 相等性判断。
struct RequestContextData {
  RequestContext context;
};

// inbox 的一次规范化 splice。wire 事件名是 agent/inbox/spliced。
//
// 持久事件先提交、活投影后变更, 于是同步观察者看到的是 splice 前的队列, 并能从
// 规范化坐标重建被移除的消息。「待处理工作」因此也是日志的投影 —— 崩溃重启后
// 仍然知道有什么没做完。
enum class InboxTarget {
  // 排队的后续 turn: FIFO, 每条各占一个 turn。wire 'next-turn'。
  NextTurn,
  // 当前 turn 的插队/注入池: turn 开头与每个 step 边界全量取走。wire 'next-step'。
  NextStep,
};

struct InboxSplicedData {
  InboxTarget target = InboxTarget::NextTurn;
  size_t start = 0;
  // 被移除的条数; 0 则不填。
  std::optional<size_t> removedCount;
  std::vector<UserMessage> inserted;
  // 'canceled': 被移除的消息算「取消」(claim 是纯删除, 不算)。dsh 键名 outcome。
  std::optional<std::string> outcome;
};

// 标记一段构造 seed 的结束。它之前的事件 seq 更小、来自 seed (resume / fork / replay),
// 本生命周期没有产出它们中的任何一条。
//
// 载荷为空 —— 位置与 time 承载全部含义。
//
// 用途: 一个独立开闭配对的拥有者 (compaction/start … compaction/end) 靠它区分 seed
// 历史与活跃工作 —— 二者在字节上完全同形。本事件之前一个未配对的开标记属于一个已经
// 结束的生命周期, 无论是什么结束了它。
//
// 只有 Session 的构造函数是合法写入方。
struct SessionEndSeedData {};

// ---- 压缩 ----

// 打开一次压缩。这条持久标记本身就是锁: 一个未配对的 start 意味着压缩正在进行,
// 或是崩溃遗留 (配合 session/end-seed 区分)。
struct CompactionStartData {
  std::string compactionId;
  // 触发它的命令 (有才有); avox 现无命令系统, 不写。
  std::optional<std::string> sourceCommandId;
  // 触发 turn; 维护相位触发时无值 —— wire 上 null 不省略 (dsh 恒写该键)。
  std::optional<int> turn;
};

// 压缩摘要的完整事实 (仅记日志)。紧随其后的是执行 surface 替换的 user/message
// (replace, sourceEventSeqs 覆盖 summary 自身与全部被遮蔽节点) —— dsh 的相邻性契约。
struct CompactionSummaryData {
  std::string compactionId;
  std::optional<std::string> sourceCommandId;
  // 摘要正文 (text 块)。
  std::vector<ContentBlock> summary;
  // 被遮蔽的 surface 节点 seq 区间 (两端均含)。
  size_t shadowedStart = 0;
  size_t shadowedEnd = 0;
  // 被遮蔽的全部 seq (surface 顺序)。
  std::vector<size_t> shadowedSeqs;
  // 遮蔽前该区间的估算 token 数。
  int64_t shadowedTokenCount = 0;
  // 写出摘要的路由。
  std::string provider;
  std::string model;
  std::optional<int> maxTokens;
  std::optional<TokenUsage> usage;
};

// 关闭一次压缩。带 error 表示这次压缩失败 (一个 start 只配一个 end)。
struct CompactionEndData {
  std::string compactionId;
  std::optional<std::string> sourceCommandId;
  std::optional<int> turn;
  std::optional<std::string> error;
};

// ---- 审批 ----

// 会话的审批策略覆盖。消费方每次读取时折叠最后一条 —— resume 不需要任何补追机制,
// 因为重放日志就是状态。
enum class ApprovalPolicy {
  // 委派给已组合的应答方 (没有应答方时 fail closed)。
  Ask,
  // 每次询问都自动拒绝, 不打扰任何人 (CI / 无人值守的确定性姿态)。
  Never,
};

struct ApprovalPolicyData {
  ApprovalPolicy policy = ApprovalPolicy::Ask;
  // 'delegation': 委派时注入子会话的覆盖; 运行时切换不带。
  std::optional<std::string> source;
};

// 一次审批询问。只带 callId 而不重复参数 —— 那次调用已经展示过了。
struct ApprovalAskedData {
  std::string id;
  std::string toolName;
  std::optional<CallId> callId;
  std::optional<std::string> reason;
};

// 询问的结果。词汇表里没有「永久允许」: 一次询问的答案只能是一次性授权,
// 持久偏好属于另一个概念 (会话策略), 不混进同一个返回值。
// wire 值: allowed-once / rejected / cancelled / unavailable。
enum class ApprovalOutcome {
  AllowedOnce,
  Rejected,
  Cancelled,
  // 无应答方、应答方抛错、或返回值不在词汇表内 —— 一律 fail closed。
  Unavailable,
};

struct ApprovalDecidedData {
  std::string id;
  ApprovalOutcome outcome = ApprovalOutcome::Unavailable;
};

// ---- todo ----

// todo 条目状态。wire 值: pending | in_progress | completed。
enum class TodoStatus {
  Pending,
  InProgress,
  Completed,
};

struct TodoItem {
  std::string content;
  TodoStatus status = TodoStatus::Pending;
};

// todo 列表快照 (wire 'todo/write'): 展示层状态, 永不上模型 surface。
//
// dsh 语义 (packages/todo/tool-todo): 整表快照, 重放时最新一条生效 (latest write wins);
// turn/start 把投影清回 null —— todo 是「当前 turn 的工作面」, 跨 turn 不保留待办。
// 日志层永远保留全部历史写入; 清空只是投影语义。
struct TodoWriteData {
  std::vector<TodoItem> todos;
};

// ---- 子代理 ----

// 子代理描述符格式版本 (dsh SUBAGENT_DESCRIPTOR_VERSION)。支持新的组合输入是
// 刻意的版本变更, 绝不隐式加字段 —— 读写两侧都只认这个值, 其它版本直接拒绝。
inline constexpr int SUBAGENT_DESCRIPTOR_VERSION = 2;

// 子代理生命周期模式: one-shot = 终态一次性运行; continuable = 可冷恢复续跑的会话。
enum class SubagentMode {
  OneShot,
  Continuable,
};

// 子代理的工具收窄 (dsh ToolRestriction)。allow 与 deny 至少声明其一。
// 自含定义于此 (不 include ToolTypes.hpp): 本文件是纯类型层, 不依赖 avox_agent
// 其余部分, 反向 include 会成环。
struct SubagentToolFilter {
  std::optional<std::vector<std::string>> allow;
  std::optional<std::vector<std::string>> deny;
};

// 子代理的持久身份与生命周期模式 (wire 事件 subagent/descriptor)。
//
// 建立子会话的 provider 在子会话首个 turn 内、首次请求之前恰好追加一条。仅记日志:
// 不带 surfaceOp, 不进模型历史, 压缩不删。枚举层靠它不重放父会话工具结果就识别出
// 子会话 (label 是创建标签, 不暴露子代理提示词)。
//
// 描述符刻意快照显式字段而非 merge-extensible 的 AgentOptions: 无关的扩展值不会
// 仅因不是 JSON 就让续跑失败。它不含 subagentDepth (冷恢复信任持久化头行的
// delegationDepth 这个单调地板), 也不含 maxTokens/outputSchema 这类只属于单次
// 激活的预算。
struct SubagentDescriptorData {
  // 描述符格式版本; 恒为 SUBAGENT_DESCRIPTOR_VERSION。
  int version = SUBAGENT_DESCRIPTOR_VERSION;
  SubagentMode mode = SubagentMode::OneShot;
  // 建立本子会话的 provider 名 (dsh ctx.subagents 的注册键)。
  std::string provider;
  // 初始委派的简短描述, 作为子会话的持久创建标签。one-shot 可选, continuable
  // 必填 (dsh 校验)。
  std::optional<std::string> label;
  // ---- continuable 专有 (avox 的 one-shot 移植不产生; 读 dsh 日志时接纳并忠实回写) ----
  // 解析后的子 agentOptions.provider / .model。
  std::optional<std::string> agentProvider;
  std::optional<std::string> agentModel;
  // 每子代理人格 (恢复时遮蔽部署人格)。
  std::optional<std::string> persona;
  // 恢复时重放的子工具收窄。
  std::optional<SubagentToolFilter> toolFilter;
};

// ---- Agent Teams (dsh experimental agent-team) ----
//
// 四种 team/* 事件只写进 **Team Lead 会话日志** (dsh TeamJournal 的落点), 全部
// 仅记日志 (不上 surface): 队伍状态是 Lead 日志的投影, 不进任何成员的模型历史。
// wire 键名与 dsh 的 SessionEventMap 声明逐字一致 —— Lead 日志两边互通。

// team/* 事件载荷的格式版本 (dsh 恒写 version:1)。其它版本响亮拒绝。
inline constexpr int TEAM_EVENT_VERSION = 1;

// 队友的持久生命周期 (dsh TeamMemberPhase: provisioning | active | failed)。
enum class TeamMemberPhase {
  Provisioning,
  Active,
  Failed,
};

// 队友会话的上下文来源 (dsh: fresh = 空白会话, fork = 继承 Lead 已完成轮次)。
enum class TeamMemberContext {
  Fresh,
  Fork,
};

// 一次队友生命周期变更写入的整份值 (wire team/member 的 member 本体)。
// name/provider/context 在 provisioning 之后不可变 (dsh fold 不变式)。
struct TeamMemberSnapshot {
  SessionId id;
  std::string name;
  std::string description;
  std::string provider;
  TeamMemberContext context = TeamMemberContext::Fresh;
  TeamMemberPhase phase = TeamMemberPhase::Provisioning;
  std::optional<std::string> error;
};

// 共享任务的持久生命周期 (dsh TeamTaskStatus)。
enum class TeamTaskStatus {
  Pending,
  InProgress,
  Completed,
  Deleted,
};

// 一份共享任务的整份快照 (wire team/task 的 task 本体)。每次变更 revision +1,
// 变更走 CAS (调用方携带 expectedRevision)。
struct TeamTaskSnapshot {
  std::string id;
  int revision = 1;
  std::string subject;
  std::string description;
  TeamTaskStatus status = TeamTaskStatus::Pending;
  std::optional<SessionId> ownerId;
  std::vector<std::string> blockedBy;
  std::vector<std::string> writeScopes;
};

// 一条待投递队友消息的整份快照 (wire team/message/queued 的 message 本体)。
// 排队先落日志, 目标会话记录后再落 delivered —— queued 减 delivered 即恢复邮箱。
enum class TeamMessageDelivery {
  // 静默: 不唤醒空闲目标 (dsh quiet, 对应 inject)。
  Quiet,
  // 唤醒: 空闲目标起一个新 turn (dsh wakeup, 对应 followup)。
  Wakeup,
};

struct TeamMessageSnapshot {
  std::string id;
  SessionId senderId;
  std::string senderName;
  SessionId targetId;
  TeamMessageDelivery delivery = TeamMessageDelivery::Quiet;
  std::vector<ContentBlock> content;
};

// team/member: 整份队友生命周期值。
struct TeamMemberEventData {
  int version = TEAM_EVENT_VERSION;
  SessionId teamId;
  TeamMemberSnapshot member;
};

// team/task: 整份共享任务值。
struct TeamTaskEventData {
  int version = TEAM_EVENT_VERSION;
  SessionId teamId;
  TeamTaskSnapshot task;
};

// team/message/queued: 持久邮箱入队 (投递尝试之前写)。
struct TeamMessageQueuedData {
  int version = TEAM_EVENT_VERSION;
  SessionId teamId;
  TeamMessageSnapshot message;
};

// team/message/delivered: 目标会话已记录该消息的持久确认。
struct TeamMessageDeliveredData {
  int version = TEAM_EVENT_VERSION;
  SessionId teamId;
  std::string messageId;
  SessionId targetId;
};

// ---- dsh 独有事件的墓碑 ----

// 装载 dsh 日志时, 「已知与 surface 无关但 avox 无语义」的事件 (hook/*、
// plan/mode 等, 见 SessionPersistence 的名单) 逐条变成本墓碑: 原始 JSON 行原样保存,
// 重写时逐字节回放。内存里它占住 seq 槽位 (seq == 下标 契约不破), Surface 天然忽略,
// 活跃 append 永不产生 (Session::appendImpl 拒绝)。
struct OpaqueEventData {
  // 原始事件类型名 (如 "todo/write")。
  std::string typeName;
  // 原始整行 JSON (信封完整, 不含换行)。重写出时原样写回。
  std::string json;
};

// ============================== 事件信封 ==============================

// 事件类型标签。顺序必须与 EventData 的变体顺序一致 (eventTypeOf 依赖这条)。
enum class EventType {
  TurnStart,
  TurnEnd,
  StepStart,
  StepEnd,
  UserMessageEvent,
  AssistantChunk,
  AssistantMessageEvent,
  ToolCall,
  ToolResult,
  RequestHeaderEvent,
  RequestContextEvent,
  InboxSpliced,
  SessionEndSeed,
  CompactionStart,
  CompactionSummary,
  CompactionEnd,
  ApprovalPolicyEvent,
  ApprovalAsked,
  ApprovalDecided,
  // todo 列表快照 (展示层状态, 永不上 surface); 见 TodoWriteData。
  TodoWrite,
  // 子代理的持久身份与生命周期模式 (仅记日志); 见 SubagentDescriptorData。
  SubagentDescriptor,
  // Agent Teams: 队友/任务/邮箱事件, 只写进 Lead 会话日志 (见上节)。
  TeamMember,
  TeamTask,
  TeamMessageQueued,
  TeamMessageDelivered,
  // dsh 独有事件的墓碑 (见 OpaqueEventData); 活跃会话永不产生。
  Opaque,
};

using EventData =
    std::variant<TurnStartData, TurnEndData, StepStartData, StepEndData,
                 UserMessageData, AssistantChunkData, AssistantMessageData,
                 ToolCallData, ToolResultData, RequestHeaderData,
                 RequestContextData, InboxSplicedData, SessionEndSeedData,
                 CompactionStartData, CompactionSummaryData, CompactionEndData,
                 ApprovalPolicyData, ApprovalAskedData, ApprovalDecidedData,
                 TodoWriteData, SubagentDescriptorData, TeamMemberEventData,
                 TeamTaskEventData, TeamMessageQueuedData, TeamMessageDeliveredData,
                 OpaqueEventData>;

// 变体顺序与 EventType 顺序一致的编译期闸门: 新增事件类型时这里会失败,
// 提醒同步更新 EventType、eventTypeName、fromEventTypeName 与所有 switch。
// (C++ 没有 dsh 的 assertNever, 这个 static_assert 是它的对应物。)
static_assert(std::variant_size_v<EventData> == 26,
              "EventData 变体数变了: 请同步 EventType、事件名映射、"
              "SessionCodec 的编解码, 以及 Surface 的投影 switch");

// 日志中的一条不可变条目。
//
// surfaceOp 与 sourceEventSeqs 是条件字段: 只有可上 surface 的事件类型
// (user/message、assistant/message、tool/result) 才允许带。非 surface 事件
// (边界标记、分片、header) 永不携带 surface 元数据 —— Session::append 在提交前校验。
//
// wire 信封**恰好** {type,seq,time,data(,surfaceOp)(,sourceEventSeqs)(,ignorable)}
// 这几个键 —— dsh 装载对信封做 exact-key 校验, 多一个键整份日志拒读。
struct SessionEvent {
  EventType type = EventType::TurnStart;
  // 会话内单调序号, 恒等于它在日志中的下标 (全系统依赖这条连续性契约)。
  size_t seq = 0;
  // Unix epoch 毫秒。
  int64_t timeMs = 0;
  EventData data;
  // 本事件如何进入 surface; 非 surface 事件不填。wire: 'append' 字符串或
  // {op:'replace',start,end} 对象。
  std::optional<SurfaceOp> surfaceOp;
  // 本事件引用的更早事件 seq (构成本消息的 chunk, 或被压缩遮蔽的 surface 节点)。
  // 不带与带空集合语义不同, 见 SurfaceIntent::sourceEventSeqs。
  std::optional<std::vector<size_t>> sourceEventSeqs;

  // 标记「读者不认识 type 时可以安全跳过本条」。
  //
  // 缺省即「必需」: 读者遇到不带此标记的未识别类型, 必须拒绝重建整个会话, 而不是
  // 静默丢弃 —— 一个未识别的必需事件可能改变其余日志的解释方式 (例如一个 replace)。
  // 写者只在纯信息性、丢失不影响重建的记录上置 true; 默认必需意味着漏打标记只会
  // 过度拒绝 (不便), 而不是静默恢复出一个被掏空的会话。
  bool ignorable = false;
};

// ============================== 事件类型工具 ==============================

// 事件的稳定 wire 名 (与 dsh 的 SessionEventMap 键完全一致, 便于对照与工具复用)。
// Opaque 返回其 typeName (装载时从原始行读出)。
const char* eventTypeName(EventType type);

// wire 名 -> 事件类型; 未识别返回 nullopt (调用方据 ignorable 决定拒绝还是跳过)。
std::optional<EventType> fromEventTypeName(const std::string& name);

// dsh KNOWN_SESSION_EVENT_TYPES 里 avox 没有语义的事件类型名 (23 种)。
//
// 装载 dsh 日志时, 这些类型逐条落成 OpaqueEventData 墓碑 (原始行原样保留, 重写时
// 逐字节回放); 名单之外的未知类型仍走「ignorable 才可跳过, 否则拒绝」的铁律。
// 逐条依据见 SessionTypes.cpp 的名单注释。
bool isDshLogOnlyEventTypeName(const std::string& name);

// 从 EventData 的变体下标推出事件类型。
EventType eventTypeOf(const EventData& data);

// 是否是可上 surface 的事件类型 —— 只有这三类才允许携带 surfaceOp。
inline bool isSurfaceEventType(EventType type) {
  return type == EventType::UserMessageEvent
      || type == EventType::AssistantMessageEvent
      || type == EventType::ToolResult;
}

// 本事件是否在 surface 上 (类型可上 surface 且确实带了标记)。
inline bool isSurfaceEvent(const SessionEvent& e) {
  return isSurfaceEventType(e.type) && e.surfaceOp.has_value();
}

// 是否是 append 来源的 surface 事件: 在自己的日志位置进入 surface, 本身从不是
// 一个替换副本。
//
// 模型可见的 surface 有意遮蔽被替换的区间, 所以它是人类 transcript 的错误来源 ——
// 一个落地的替换会擦掉用户已经看过的对话。append 来源的事件才是 transcript 的持久
// 素材, 替换副本只对模型可见。(avox shell 展示历史时必须走这个判定, 不能走 surface。)
inline bool isAppendSurfaceEvent(const SessionEvent& e) {
  return isSurfaceEvent(e) && std::holds_alternative<SurfaceAppend>(*e.surfaceOp);
}

// 是否是一次 surface 替换: 遮蔽了既有区间而非追加到尾部。
inline bool isReplacementSurfaceEvent(const SessionEvent& e) {
  return isSurfaceEvent(e) && std::holds_alternative<SurfaceReplace>(*e.surfaceOp);
}

}
