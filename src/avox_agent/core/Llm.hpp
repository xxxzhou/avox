#pragma once

// ============================================================================
// LLM 能力接缝。
//
// 对齐 dsh 的 packages/llm (Service Definition 部分)。驱动只认这个接口, 不认任何具体
// provider —— 于是免费模型轮换、重试退避、路由降级全都能作为策略挂在扩展点上, 而不必
// 写进驱动或 SSE 分帧代码里。
//
// prepareCall 与 stream 分开的理由: 「解析路由并物化 adapter 默认值」是一次可失败、
// 需要在写 request/header 之前完成的动作。header 要记的是**物化之后**的配置, 否则
// 「header 不变 ⟺ 请求前缀字节相同」这条 KV cache 判据就不成立。
// ============================================================================

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Abort.hpp"
#include "SessionTypes.hpp"

namespace avox {

// 一次请求的完整输入。
struct LlmRequest {
  LlmCallConfig config;
  // 渲染后的完整 system 文本; 空串表示无 system。
  std::string system;
  // 工具 schema 数组 JSON; 空串表示无工具。
  std::string toolsJson;
  // 派生的模型历史 (来自 Session::deriveMessages)。
  std::vector<Message> messages;
  std::shared_ptr<AbortSignal> signal;
};

// 流式分片回调。
//
// 在 provider 的读取线程上同步调用, 必须短小: 驱动在这里把分片落进日志并转发给 UI。
// 分片即 dsh 的 StreamChunk (7 变体) —— 驱动逐分片落 assistant/chunk 事件, token 级
// 回放保真; usage/finish 分片同样经过这里 (它们也进日志)。
struct LlmStreamHandler {
  std::function<void(const StreamChunk& chunk)> onChunk;
};

// 一次请求的结束方式。
enum class LlmFinishKind {
  // 模型自然结束 (stop / tool_calls)。
  Completed,
  // 撞到输出上限。
  MaxTokens,
  // 失败 (HTTP 错误、解析失败、上游异常)。
  Error,
  // 被取消。
  Aborted,
};

struct LlmFinish {
  LlmFinishKind kind = LlmFinishKind::Completed;
  // kind == Error 时有效。
  std::optional<LlmFailure> failure;
  // 后端上报了 token 记账时有效。
  std::optional<TokenUsage> usage;
};

// 一次已解析路由的调用。
//
// adapterDefaults 标记哪些字段是 adapter 物化的而非调用方提议的 —— 驱动据此在把折叠出的
// header 喂给下一次 agent/request 之前删掉它们, 于是切换路由后新 adapter 会重新物化自己
// 的默认值, 而用户显式设的值跨 step、跨路由保留。(免费模型轮换直接吃这条: 轮到另一个
// 模型时 maxTokens 该重算, temperature 该保留。)
struct PreparedLlmCall {
  LlmCallConfig config;
  LlmCallConfigAdapterDefaults adapterDefaults;
  std::optional<int64_t> contextWindow;
};

class LlmProvider {
 public:
  virtual ~LlmProvider() = default;

  // 解析路由并物化默认值。路由不可用时抛 std::runtime_error。
  virtual PreparedLlmCall prepareCall(const LlmCallConfig& proposed) = 0;

  // 发一次请求并把分片交给 handler。
  //
  // **同步阻塞**直到流结束或被取消。取消时必须让已建立的连接与线程到达静止后才返回 ——
  // 驱动不会抛弃这个调用。
  virtual LlmFinish stream(const LlmRequest& request,
                          const LlmStreamHandler& handler) = 0;
};

// 把流式分片组装成一条 assistant 消息。
//
// 对齐 dsh 的 BlockAssembler: 分片按块索引归属 (block-start 开槽, delta 追加到槽,
// block-end 定稿), 块在消息 content 里的顺序 = 块首次出现的顺序 —— 文本与工具调用
// 交错时, 顺序就是模型的输出顺序。工具调用的 arguments 由分片累积, 流式期间不 parse,
// 攒齐再交给工具。
//
// usage 与 finish 分片不属于内容, 进来直接忽略 (它们只进日志)。
class BlockAssembler {
 public:
  void push(const StreamChunk& chunk);

  // 组装结果 (可移动取出)。
  const std::vector<ContentBlock>& blocks() const { return assembled; }
  std::vector<ContentBlock> take() { return std::move(assembled); }

  // 是否含至少一个工具调用。
  bool hasToolCalls() const;

  // 工具调用块 (按出现顺序)。
  std::vector<ToolCallBlock> toolCalls() const;

  // 被取消流的已送达前缀 (dsh interruptedBlocks): 按流顺序返回内容非空白的
  // text 与 reasoning 块 —— 已闭合与未闭合都算。打断先于分派, 没有真实工具
  // 结果, 故省略 tool-call 块; 空白块与空结果同样省略。空结果 = 不追加消息。
  std::vector<ContentBlock> interruptedBlocks() const;

 private:
  // 取到该块索引的槽位下标, 没开过槽就先开 (宽容: 个别后端不发 block-start)。
  size_t slot(int index);

  std::vector<ContentBlock> assembled;
  // 块索引 -> assembled 下标。dsh 协议里索引把交错的 delta 关联回块, 是组装的键。
  std::map<int, size_t> slotOf;
};

}
