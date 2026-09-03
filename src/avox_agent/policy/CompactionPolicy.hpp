#pragma once

// ============================================================================
// 上下文压缩。
//
// 对齐 dsh 的 packages/compaction/compaction-basic。挂在 agent/pre-step 上。
//
// 与旧实现 (已删的 AgentContext) 的两个关键差异:
//   1. **压缩不删历史**。旧 AgentContext::compressMessage 直接 m.text = stub 原地覆盖,
//      原文没了。这里的产出是 surface 上的一个 replace 节点: 原事件永久保留, 审计与回放
//      仍能看到原始轮次, 而模型历史立刻变短。
//   2. **切点必须让工具配对平衡**。保留区的首节点不能是 tool/result —— 它的 tool_call 在
//      被压缩的 assistant 消息里, 留下一个孤立的 tool 消息会让下一次请求在后端侧非法。
//      旧实现之所以没这个问题, 是因为工具结果压根没进历史 (那本身是要修的 bug)。
//
// 占用量判据用**上一次请求的真实 promptTokens** (从最近一条带 usage 的 assistant/message
// 折叠), 而不是字符数估算 —— 后者在中文与 JSON 混排的日志里能差出两三倍。
// ============================================================================

#include <string>

#include "avox_agent/core/Agent.hpp"
#include "avox_agent/core/Llm.hpp"
#include "avox_agent/core/Scope.hpp"

namespace avox {

struct CompactionPolicyConfig {
  // 触发阈值: 占用量超过 contextWindow 的这个比例就压缩。
  double thresholdRatio = 0.7;

  // 会话未公布 contextWindow 时的假设值。
  int64_t fallbackContextWindow = 65536;

  // 至少保留的尾部 surface 节点数。
  //
  // 保住最近的对话不被压掉 —— 那是模型正在做的事, 压掉它等于让模型失忆。
  int keepTailNodes = 6;

  // 摘要指令。
  std::string summaryPrompt =
      "请把上面的对话历史压缩成一段简洁的工作纪要。保留: 用户的目标与约束、已确认的事实"
      "与结论、已经做过的关键操作及其结果、尚未完成的事项。丢弃: 寒暄、重复的中间步骤、"
      "已被更新结论取代的旧判断。用第三人称陈述, 不要加评论。";

  // 摘要用的模型 (空 = 用会话当前路由)。
  //
  // 单独可配的理由: 摘要是纯文本任务, 用一个便宜快速的模型足够, 而主对话可能在用贵模型。
  std::string summaryModel;
};

// 安装压缩策略; 返回撤销器。
Disposer installCompactionPolicy(AgentExtensionPoints& points, LlmProvider& llm,
                                CompactionPolicyConfig config);

}
