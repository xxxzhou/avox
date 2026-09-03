#include "CompactionPolicy.hpp"

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "PolicySupport.hpp"
#include "avox/module/LogHelper.hpp"

namespace avox {

namespace {

// dsh 检查点前导 (compaction-basic/summarizer.ts CHECKPOINT_PREAMBLE), 逐字照抄:
// 两边 resume 看到的检查点形状一致, 续跑行为才一致。
const char* kCheckpointPreamble =
    "This is an automatically generated checkpoint condensing an earlier span "
    "of the conversation to free up context. Treat the captured context as "
    "established background and build on it without restating it. Continue the "
    "task directly from the messages that follow, without acknowledging this "
    "checkpoint.";

// 当前上下文占用量: 最近一条带 usage 的 assistant/message 的记账。
//
// dsh 的 inputTokens 只含未命中缓存的部分, 命中的记在 cacheReadTokens —— 但对上下文
// 窗口而言两者都占位, 所以相加。那是上一次请求实际消耗的输入规模, 比任何字符估算都
// 准。没有 usage (首轮、后端不报) 时返回 0 = 不触发压缩 —— 宁可不压也不要凭猜测砍掉
// 历史。
int64_t currentPromptTokens(const Session& session) {
  const std::vector<SessionEvent>& events = session.events();
  for (size_t i = events.size(); i > 0; --i) {
    const SessionEvent& event = events[i - 1];
    if (event.type != EventType::AssistantMessageEvent) continue;
    const auto& data = std::get<AssistantMessageData>(event.data);
    if (!data.usage.has_value()) continue;
    return data.usage->inputTokens + data.usage->cacheReadTokens.value_or(0);
  }
  return 0;
}

int64_t contextWindowOf(const Session& session, int64_t fallback) {
  const std::vector<SessionEvent>& events = session.events();
  for (size_t i = events.size(); i > 0; --i) {
    const SessionEvent& event = events[i - 1];
    if (event.type != EventType::RequestContextEvent) continue;
    const auto& data = std::get<RequestContextData>(event.data);
    if (data.context.contextWindow.has_value()) return *data.context.contextWindow;
    break;
  }
  return fallback;
}

// ---- dsh token-meter 固定密度估算器 (token-meter/src/estimate.ts) 的移植 ----
//
// shadow price 契约: dsh 折叠侧 (surface-projection.ts) 给每条表面 append 事件按
// 这个估算器计价, replace 时按 claim 扣减 —— 生产者必须用同一估算器对被遮蔽节点
// 逐条求和 (compaction-basic/region.ts:354 selectedNodes.reduce)。用别的口径 (如
// 真实 usage, 含 system/tools/保留区) 会让 dsh 重放时加/减不对称: 实测真实 usage
// 68279 vs 估算和 1983, 扣出负数被 zod nonnegative 拒载 (history unavailable)。
// 常数是 wire 契约, 不是可调参数。
constexpr int64_t kCharsPerToken = 4;
constexpr int64_t kBlockOverhead = 4;
constexpr int64_t kRoleOverhead = 4;

int64_t textPrice(const std::string& text) {
  return static_cast<int64_t>((text.size() + 3) / 4) + kBlockOverhead;
}

// image 走 dsh estimateContent 的 default 分支: 解码后块的 JSON.stringify 长度。
// avox 的 ImageBlock 与 dsh 同构 (type + attachment, 字段序一致), 按同一形状手工
// 序列化; name 缺省省略 (JSON.stringify 跳过 undefined, 两侧一致)。
int64_t imagePrice(const ImageAttachmentRef& attachment) {
  std::string json = "{\"type\":\"image\",\"attachment\":{\"attachmentId\":\""
      + attachment.attachmentId + "\",\"mediaType\":\"" + attachment.mediaType
      + "\",\"bytes\":" + std::to_string(attachment.bytes)
      + ",\"width\":" + std::to_string(attachment.width)
      + ",\"height\":" + std::to_string(attachment.height);
  if (attachment.name.has_value()) {
    json += ",\"name\":\"" + *attachment.name + "\"";
  }
  json += "}}";
  return static_cast<int64_t>((json.size() + 3) / 4) + kBlockOverhead;
}

int64_t estimateBlocks(const std::vector<ToolResultContent>& content) {
  int64_t tokens = 0;
  for (const ToolResultContent& item : content) {
    if (const auto* text = std::get_if<TextBlock>(&item)) {
      tokens += textPrice(text->text);
    } else if (const auto* reasoning = std::get_if<ReasoningBlock>(&item)) {
      tokens += textPrice(reasoning->text);
    } else if (const auto* image = std::get_if<ImageBlock>(&item)) {
      tokens += imagePrice(image->attachment);
    }
  }
  return tokens;
}

int64_t estimateBlocks(const std::vector<ContentBlock>& content) {
  int64_t tokens = 0;
  for (const ContentBlock& block : content) {
    if (const auto* text = std::get_if<TextBlock>(&block)) {
      tokens += textPrice(text->text);
    } else if (const auto* reasoning = std::get_if<ReasoningBlock>(&block)) {
      tokens += textPrice(reasoning->text);
    } else if (const auto* call = std::get_if<ToolCallBlock>(&block)) {
      // dsh 只按 name+arguments 计价 (id 不计)。
      tokens += static_cast<int64_t>((call->name.size() + 3) / 4)
          + static_cast<int64_t>((call->arguments.size() + 3) / 4) + kBlockOverhead;
    } else if (const auto* result = std::get_if<ToolResultBlock>(&block)) {
      tokens += estimateBlocks(result->content) + kBlockOverhead;
    } else if (const auto* image = std::get_if<ImageBlock>(&block)) {
      tokens += imagePrice(image->attachment);
    }
  }
  return tokens;
}

// 一条表面事件的估算价 (estimate.ts estimateMessage + surface.ts
// deriveEventMessage): 三类消息事件按内容块求和加角色开销; 空内容 assistant
// 消息不上表面, 计 0。
int64_t estimateSurfaceEvent(const SessionEvent& event) {
  switch (event.type) {
    case EventType::UserMessageEvent: {
      const std::vector<ContentBlock>& content =
          std::get<UserMessageData>(event.data).message.content;
      return estimateBlocks(content) + kRoleOverhead;
    }
    case EventType::AssistantMessageEvent: {
      const std::vector<ContentBlock>& content =
          std::get<AssistantMessageData>(event.data).message.content;
      if (content.empty()) return 0;
      return estimateBlocks(content) + kRoleOverhead;
    }
    case EventType::ToolResult: {
      const std::vector<ToolResultBlock>& content =
          std::get<ToolResultData>(event.data).message.content;
      int64_t tokens = 0;
      for (const ToolResultBlock& block : content) {
        tokens += estimateBlocks(block.content) + kBlockOverhead;
      }
      return tokens + kRoleOverhead;
    }
    default:
      return 0;
  }
}

// 是否有未闭合的 compaction/start。
//
// 这条持久标记本身就是锁: 一个未配对的 start 意味着压缩正在进行, 或是崩溃遗留。
// 后者靠 session/end-seed 区分 —— 它之前的未配对 start 属于一个已经结束的生命周期。
bool hasActiveCompaction(const Session& session) {
  const std::vector<SessionEvent>& events = session.events();
  size_t liveFrom = 0;
  for (size_t i = events.size(); i > 0; --i) {
    if (events[i - 1].type == EventType::SessionEndSeed) {
      liveFrom = i;
      break;
    }
  }
  int open = 0;
  for (size_t i = liveFrom; i < events.size(); ++i) {
    if (events[i].type == EventType::CompactionStart) ++open;
    if (events[i].type == EventType::CompactionEnd) --open;
  }
  return open > 0;
}

// 选出可压缩区间 [0, cut) 的末端节点索引; 返回 false 表示不值得压。
bool selectCompactableRange(Session& session, int keepTailNodes, size_t& cutIndex) {
  const std::vector<size_t>& nodes = session.getSurface().nodes();
  const size_t keep = static_cast<size_t>(keepTailNodes < 1 ? 1 : keepTailNodes);
  if (nodes.size() <= keep + 1) return false;

  size_t cut = nodes.size() - keep;

  // 把切点继续前移, 直到保留区的首节点不是 tool/result。
  //
  // 一个 tool/result 的 tool_call 在它前面那条 assistant 消息里。若切点把 assistant 划进
  // 压缩区而 tool/result 留在保留区, 保留区就有一个孤立的 tool 消息, 下一次请求在后端侧
  // 直接非法。
  const std::vector<SessionEvent>& events = session.events();
  while (cut > 0 && events[nodes[cut]].type == EventType::ToolResult) --cut;

  // 压缩区太短就不值得: 一次摘要请求的成本换不回几条消息。
  if (cut < 2) return false;
  cutIndex = cut;
  return true;
}

// 把一段历史渲染成给摘要模型看的文本。
//
// 参数非 const: 读 surface 会惰性追平日志, 那是一次内部状态更新。
std::string renderForSummary(Session& session, size_t cutIndex) {
  const std::vector<size_t>& nodes = session.getSurface().nodes();
  const std::vector<SessionEvent>& events = session.events();
  std::string rendered;
  for (size_t i = 0; i < cutIndex; ++i) {
    const SessionEvent& event = events[nodes[i]];
    const char* role = "user";
    std::string text;
    switch (event.type) {
      case EventType::UserMessageEvent: {
        const UserMessage& message = std::get<UserMessageData>(event.data).message;
        role = message.source.kind == MessageSourceKind::User ? "用户" : "系统注入";
        for (const ContentBlock& block : message.content) {
          if (const auto* t = std::get_if<TextBlock>(&block)) text += t->text;
        }
        break;
      }
      case EventType::AssistantMessageEvent: {
        role = "助手";
        const AssistantMessage& message =
            std::get<AssistantMessageData>(event.data).message;
        for (const ContentBlock& block : message.content) {
          if (const auto* t = std::get_if<TextBlock>(&block)) {
            text += t->text;
          } else if (const auto* call = std::get_if<ToolCallBlock>(&block)) {
            text += "\n[调用工具 " + call->name + " " + call->arguments + "]";
          }
        }
        break;
      }
      case EventType::ToolResult: {
        role = "工具结果";
        const ToolResultMessage& message =
            std::get<ToolResultData>(event.data).message;
        // 两层结构: 消息 content 是 ToolResultBlock, 块内才是正文条目
        // (text/reasoning/image —— 这里只取 text)。
        for (const ToolResultBlock& block : message.content) {
          for (const ToolResultContent& item : block.content) {
            if (const auto* t = std::get_if<TextBlock>(&item)) text += t->text;
          }
        }
        break;
      }
      default:
        continue;
    }
    if (text.empty()) continue;
    rendered += std::string("\n\n") + role + ": " + text;
  }
  return rendered;
}

}  // namespace

Disposer installCompactionPolicy(AgentExtensionPoints& points, LlmProvider& llm,
                                CompactionPolicyConfig config) {
  if (config.thresholdRatio <= 0.0 || config.thresholdRatio >= 1.0) {
    throw std::runtime_error("thresholdRatio 必须在 (0, 1) 之间");
  }
  if (config.keepTailNodes < 1) {
    throw std::runtime_error("keepTailNodes 必须是正整数");
  }
  if (config.fallbackContextWindow < 1024) {
    throw std::runtime_error("fallbackContextWindow 过小");
  }

  auto configShared = std::make_shared<CompactionPolicyConfig>(std::move(config));
  auto counter = std::make_shared<size_t>(0);

  return points.preStep.on(
      [&llm, configShared, counter](
          PreStepPayload& payload,
          const Chain<PreStepPayload, PreStepDecision>::Next& next)
          -> PreStepDecision {
        Agent* agent = payload.agent;
        if (agent == nullptr) return next();

        // ---- 判定 (锁内, 只读) ----
        bool shouldCompact = false;
        size_t cutIndex = 0;
        size_t startSeq = 0;
        size_t endSeq = 0;
        int64_t occupied = 0;
        int64_t shadowPrice = 0;
        std::string history;
        std::vector<size_t> shadowedSeqs;
        size_t generation = 0;

        agent->withSession([&](Session& session) {
          if (hasActiveCompaction(session)) return;
          occupied = currentPromptTokens(session);
          if (occupied == 0) return;
          const int64_t window =
              contextWindowOf(session, configShared->fallbackContextWindow);
          const int64_t threshold =
              static_cast<int64_t>(static_cast<double>(window)
                                   * configShared->thresholdRatio);
          if (occupied < threshold) return;
          if (!selectCompactableRange(session, configShared->keepTailNodes,
                                      cutIndex)) {
            return;
          }
          const std::vector<SessionEvent>& events = session.events();
          const std::vector<size_t>& nodes = session.getSurface().nodes();
          // shadow price 必须按估算器对被遮蔽节点求和, 不能用 occupied (见上)。
          for (size_t i = 0; i < cutIndex; ++i) {
            shadowPrice += estimateSurfaceEvent(events[nodes[i]]);
          }
          startSeq = nodes.front();
          endSeq = nodes[cutIndex - 1];
          shadowedSeqs.assign(nodes.begin(), nodes.begin() + static_cast<ptrdiff_t>(cutIndex));
          generation = session.getSurface().replaceGeneration();
          history = renderForSummary(session, cutIndex);
          shouldCompact = true;
        });

        if (!shouldCompact) return next();

        const std::string compactionId =
            agent->id().value + "/compaction/" + std::to_string(++*counter);

        // ---- 开标记 (锁内) ----
        // 这条持久标记就是锁: 它未配对期间别的 pre-step 不会再启动一次压缩。
        // seq 要记下 —— dsh 把它放进替换消息的 provenance (region.ts:464)。
        size_t startMarkerSeq = 0;
        agent->withSession([&](Session& session) {
          CompactionStartData start;
          start.compactionId = compactionId;
          start.turn = payload.turn;
          startMarkerSeq = session.append(std::move(start));
        });

        // ---- 生成摘要 (锁外: 要发一次模型请求) ----
        std::string summary;
        std::string failure;
        LlmCallConfig routedConfig;
        std::optional<TokenUsage> summaryUsage;
        try {
          LlmCallConfig summaryConfig;
          agent->withSession([&](Session& session) {
            const EpochHeader* header = session.requestHeader();
            if (header != nullptr) summaryConfig = header->config;
          });
          if (summaryConfig.provider.empty()) {
            summaryConfig.provider = agent->options().provider;
            summaryConfig.model = agent->options().model;
          }
          if (!configShared->summaryModel.empty()) {
            summaryConfig.model = configShared->summaryModel;
          }

          LlmRequest request;
          request.config = llm.prepareCall(summaryConfig).config;
          routedConfig = request.config;
          request.system = configShared->summaryPrompt;
          UserMessage input;
          input.id = MessageId(compactionId + "/input");
          input.content.push_back(TextBlock{history});
          // dsh 的 summarizer 把摘要请求输入标为 plugin:'dsh-compaction-basic'
          // (summarizer.ts); 它只进 provider 请求不进会话日志, 这里照抄路由习惯。
          input.source = pluginSource("dsh-compaction-basic");
          request.messages.push_back(input);
          request.signal = payload.signal;

          BlockAssembler assembler;
          LlmStreamHandler handler;
          handler.onChunk = [&assembler](const StreamChunk& chunk) {
            assembler.push(chunk);
          };
          const LlmFinish finish = llm.stream(request, handler);
          if (finish.kind == LlmFinishKind::Error
              || finish.kind == LlmFinishKind::Aborted) {
            failure = finish.failure.has_value() ? finish.failure->message
                                                 : "摘要请求被取消";
          } else {
            for (const ContentBlock& block : assembler.blocks()) {
              if (const auto* text = std::get_if<TextBlock>(&block)) {
                summary += text->text;
              }
            }
            if (summary.empty()) {
              failure = "摘要模型返回空内容";
            } else {
              summaryUsage = finish.usage;
            }
          }
        } catch (const std::exception& e) {
          failure = e.what();
        }

        // ---- 提交 (锁内) ----
        bool committed = false;
        agent->withSession([&](Session& session) {
          if (!failure.empty()) return;
          // 表面在生成摘要期间变了就放弃本次: 区间坐标已经不再指向同一段历史。
          // (当前实现里驱动是唯一写者且正在此处, 所以这只是一道不变式护栏。)
          if (session.getSurface().replaceGeneration() != generation) {
            failure = "表面在压缩期间发生变化, 放弃本次压缩";
            return;
          }

          CompactionSummaryData summaryData;
          summaryData.compactionId = compactionId;
          summaryData.summary.push_back(TextBlock{summary});
          summaryData.shadowedStart = startSeq;
          summaryData.shadowedEnd = endSeq;
          summaryData.shadowedSeqs = shadowedSeqs;
          summaryData.shadowedTokenCount = shadowPrice;
          summaryData.provider = routedConfig.provider;
          summaryData.model = routedConfig.model;
          summaryData.maxTokens = routedConfig.maxTokens;
          summaryData.usage = summaryUsage;
          const size_t summarySeq = session.append(std::move(summaryData));

          // 检查点消息: dsh frameSummary 的三段式 —— 前导+开标签 / 摘要正文 /
          // 闭标签 (summarizer.ts), source 是 COMPACT_CHECKPOINT_MARKER
          // (plugin:'compact' + compactionId, compaction/checkpoint.ts)。
          UserMessage replacement;
          replacement.id = MessageId(compactionId + "/summary");
          replacement.content.push_back(TextBlock{
              std::string(kCheckpointPreamble) + "\n\n<compacted-summary>"});
          replacement.content.push_back(TextBlock{summary});
          replacement.content.push_back(TextBlock{"</compacted-summary>"});
          replacement.source = compactCheckpointSource(compactionId);

          // provenance 逐字照抄 dsh (region.ts:464): [start 标记, summary, ...被遮蔽]。
          // start 标记与 summary 都不是 surface 节点, 与 shadowedSeqs 不会重复。
          std::vector<size_t> sources;
          sources.reserve(shadowedSeqs.size() + 2);
          sources.push_back(startMarkerSeq);
          sources.push_back(summarySeq);
          sources.insert(sources.end(), shadowedSeqs.begin(), shadowedSeqs.end());
          SurfaceIntent intent;
          intent.surfaceOp = SurfaceReplace{startSeq, endSeq};
          intent.sourceEventSeqs = std::move(sources);
          session.append(UserMessageData{std::move(replacement)}, intent);
          committed = true;
        });

        // ---- 闭标记 (锁内) ----
        agent->withSession([&](Session& session) {
          CompactionEndData end;
          end.compactionId = compactionId;
          end.turn = payload.turn;
          if (!failure.empty()) end.error = failure;
          session.append(std::move(end));
        });

        if (!committed) {
          // 压缩失败不阻塞这一步: 上下文仍然偏大, 但让模型继续跑总比卡住好 ——
          // 下一步会再试一次。
          LOGFLF(LogLevel::warn, "[compaction] 压缩未完成: ", failure.c_str());
        }
        return next();
      });
}

}
