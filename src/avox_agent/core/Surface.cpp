#include "Surface.hpp"

#include <algorithm>
#include <set>
#include <stdexcept>
#include <string>

namespace avox {

// ===========================================================================
// 投影
// ===========================================================================

std::optional<Message> deriveEventMessage(const SessionEvent& event) {
  switch (event.type) {
    // 普通提示与注入上下文都以 user 角色投影: 事件的模型可见内容原样保留。
    //
    // 不要在这里按类型重新加包装 (例如 <context> 标签): 包装归产出方所有 —— 由产出方
    // 烘进 content 里。让本投影保持逐字透传, 否则「模型可见 ⟺ 已记录」就出现了一层
    // 只存在于投影期的隐形内容。
    case EventType::UserMessageEvent:
      return std::get<UserMessageData>(event.data).message;

    case EventType::AssistantMessageEvent: {
      const auto& data = std::get<AssistantMessageData>(event.data);
      // 跳过空 content 的 assistant/message: 它只为承载一个 max-tokens step 的 usage
      // 而存在, 不能往 provider transcript 里塞一条没有内容的 assistant 轮次。
      if (data.message.content.empty()) return std::nullopt;
      return data.message;
    }

    case EventType::ToolResult:
      return std::get<ToolResultData>(event.data).message;

    default:
      // 非 surface 事件 (边界、分片、仅记日志的记录) 不产出消息。
      // 词汇表是可扩展的, 所以这里是一个有文档的默认分支, 不是穷尽性缺口。
      return std::nullopt;
  }
}

// ===========================================================================
// 校验
// ===========================================================================

namespace {

// 一次已校验但尚未提交的替换计划。
struct SurfaceReplacePlan {
  SurfaceFoldReplacement replacement;
  size_t startIdx = 0;
  size_t endIdx = 0;
};

struct SurfacePlan {
  enum class Kind { None, Append, Replace } kind = Kind::None;
  size_t seq = 0;
  SurfaceReplacePlan replace;
};

// 取出并校验 surface 元数据的基本合法性。
// 返回 nullopt 表示本事件不上 surface。
std::optional<SurfaceOp> readSurfaceOp(const SessionEvent& event) {
  if (!event.surfaceOp.has_value()) {
    // 反向约束: 不上 surface 的事件不得携带来源引用之外的 surface 语义。
    // (sourceEventSeqs 本身允许独立存在, 例如将来某个仅记日志的事件想标注来源。)
    return std::nullopt;
  }
  if (!isSurfaceEventType(event.type)) {
    throw std::runtime_error(
        std::string("session event \"") + eventTypeName(event.type)
        + "\" 不是可上 surface 的类型, 不得携带 surfaceOp");
  }
  return *event.surfaceOp;
}

// 校验来源引用: 相对本事件必须更早、不得重复、非空要求, 以及 replace 时必须覆盖
// 每一个被遮蔽节点。
void assertProvenance(const SessionEvent& event,
                      const std::vector<size_t>& shadowedSeqs) {
  std::set<size_t> sources;
  if (event.sourceEventSeqs.has_value()) {
    const auto& raw = *event.sourceEventSeqs;
    // 只有 assistant/message 允许显式空集合 (已知的空 provider 流)。
    if (raw.empty() && event.type != EventType::AssistantMessageEvent) {
      throw std::runtime_error(
          "sourceEventSeqs 除 assistant/message 外不得为空集合");
    }
    std::optional<size_t> nonEarlier;
    for (size_t source : raw) {
      sources.insert(source);
      // 引用必须指向更早的事件: 否则一次 replace 可以引用自己或未来, 折叠就不再可重放。
      if (!nonEarlier.has_value() && source >= event.seq) nonEarlier = source;
    }
    if (sources.size() != raw.size()) {
      throw std::runtime_error("sourceEventSeqs 不得含重复项");
    }
    if (nonEarlier.has_value()) {
      throw std::runtime_error(
          "sourceEventSeqs 必须引用更早的事件: " + std::to_string(*nonEarlier)
          + " >= 当前 seq " + std::to_string(event.seq));
    }
  }

  std::vector<size_t> missing;
  for (size_t seq : shadowedSeqs) {
    if (sources.find(seq) == sources.end()) missing.push_back(seq);
  }
  if (!missing.empty()) {
    std::string list;
    for (size_t i = 0; i < missing.size(); ++i) {
      if (i > 0) list += ", ";
      list += std::to_string(missing[i]);
    }
    throw std::runtime_error(
        "surface replace: sourceEventSeqs 必须覆盖每一个被遮蔽的表面节点; 缺少 " + list);
  }
}

// 在不改动状态的前提下定位一次替换区间。
SurfaceReplacePlan replacementRange(const std::vector<size_t>& nodes,
                                    const SurfaceReplace& op, size_t seq) {
  const auto startIt = std::find(nodes.begin(), nodes.end(), op.start);
  if (startIt == nodes.end()) {
    throw std::runtime_error("surface replace: 起点 seq "
                             + std::to_string(op.start) + " 不在当前表面上");
  }
  const auto endIt = std::find(nodes.begin(), nodes.end(), op.end);
  if (endIt == nodes.end()) {
    throw std::runtime_error("surface replace: 终点 seq "
                             + std::to_string(op.end) + " 不在当前表面上");
  }
  const size_t startIdx = static_cast<size_t>(startIt - nodes.begin());
  const size_t endIdx = static_cast<size_t>(endIt - nodes.begin());
  if (startIdx > endIdx) {
    throw std::runtime_error(
        "surface replace: 起点 seq " + std::to_string(op.start) + " (下标 "
        + std::to_string(startIdx) + ") 在终点 seq " + std::to_string(op.end)
        + " (下标 " + std::to_string(endIdx) + ") 之后");
  }

  SurfaceReplacePlan plan;
  plan.startIdx = startIdx;
  plan.endIdx = endIdx;
  plan.replacement.seq = seq;
  plan.replacement.start = op.start;
  plan.replacement.end = op.end;
  plan.replacement.shadowedSeqs.assign(nodes.begin() + startIdx,
                                       nodes.begin() + endIdx + 1);
  return plan;
}

bool sameToolResultError(const std::optional<ToolResultError>& a,
                         const std::optional<ToolResultError>& b) {
  if (a.has_value() != b.has_value()) return false;
  if (!a.has_value()) return true;
  return a->name == b->name && a->code == b->code;
}

// 把 tool/result 的替换限制为「只改这一次结果的正文」。
//
// 这是给结果裁剪 (压缩阶段的 tool-result pruner) 开的窄口子: 它需要把一条历史工具结果
// 换成更短的文本, 但绝不能改这次调用的身份或成败 —— 那会让日志与模型看到的因果关系
// 不一致。
//
// 与 dsh 的偏离: dsh 用 JSON 深比较「把 content 置空后剩余部分」, 本实现改为逐字段比较
// 且允许 content 的块数变化。语义更清楚 (正文可改、身份与成败不可改), 代价是比 dsh 略宽松。
void assertToolResultRewrite(const SessionEvent& event,
                             const std::vector<size_t>& shadowedSeqs,
                             const std::vector<SessionEvent>& eventLog) {
  if (event.type != EventType::ToolResult) return;
  if (shadowedSeqs.size() != 1) {
    throw std::runtime_error("tool/result 的表面替换必须恰好重写一个当前节点");
  }
  const size_t originalSeq = shadowedSeqs[0];
  if (originalSeq >= eventLog.size()) {
    throw std::runtime_error("tool/result 的表面替换目标 seq "
                             + std::to_string(originalSeq) + " 不在日志内");
  }
  const SessionEvent& original = eventLog[originalSeq];
  if (original.type != EventType::ToolResult) {
    throw std::runtime_error("tool/result 的表面替换目标必须是一条 tool/result");
  }

  const auto& a = std::get<ToolResultData>(original.data);
  const auto& b = std::get<ToolResultData>(event.data);
  // 成败标志在块上 (恰好一个块): 空内容视作非错。
  const auto isErrorOf = [](const ToolResultMessage& m) {
    return !m.content.empty() && m.content.front().isError;
  };
  const bool identitySame =
      a.turn == b.turn && a.step == b.step && a.message.id == b.message.id
      && a.message.source.callId == b.message.source.callId
      && isErrorOf(a.message) == isErrorOf(b.message)
      && sameToolResultError(a.error, b.error) && a.meta == b.meta;
  if (!identitySame) {
    throw std::runtime_error("tool/result 的表面替换只能改结果正文");
  }
}

// 规划一条事件的表面转移 (不改状态)。
SurfacePlan planSurfaceEvent(const std::vector<size_t>& nodes,
                             const SessionEvent& event, size_t expectedSeq,
                             const std::vector<SessionEvent>& eventLog) {
  if (event.seq != expectedSeq) {
    throw std::runtime_error("session event seq " + std::to_string(event.seq)
                             + " 与期望位置 " + std::to_string(expectedSeq)
                             + " 不符; seq 必须恒等于日志下标");
  }

  const std::optional<SurfaceOp> op = readSurfaceOp(event);
  if (!op.has_value()) {
    // 非表面事件仍需校验来源引用的自洽性 (更早、不重复)。
    assertProvenance(event, {});
    return {};
  }

  SurfacePlan plan;
  plan.seq = event.seq;
  if (std::holds_alternative<SurfaceAppend>(*op)) {
    assertProvenance(event, {});
    plan.kind = SurfacePlan::Kind::Append;
    return plan;
  }

  plan.replace =
      replacementRange(nodes, std::get<SurfaceReplace>(*op), event.seq);
  assertProvenance(event, plan.replace.replacement.shadowedSeqs);
  assertToolResultRewrite(event, plan.replace.replacement.shadowedSeqs, eventLog);
  plan.kind = SurfacePlan::Kind::Replace;
  return plan;
}

// 提交一个已规划的转移; 只有 replace 会产出替换记录。
std::optional<SurfaceFoldReplacement> applySurfacePlan(
    std::vector<size_t>& nodes, size_t& replaceGeneration,
    const SurfacePlan& plan) {
  switch (plan.kind) {
    case SurfacePlan::Kind::Append:
      nodes.push_back(plan.seq);
      return std::nullopt;

    case SurfacePlan::Kind::Replace: {
      const auto first = nodes.begin() + plan.replace.startIdx;
      const auto last = nodes.begin() + plan.replace.endIdx + 1;
      // 用本节点顶掉整段: 被遮蔽的节点从派生历史里消失, 但它们的事件仍在日志里。
      const auto it = nodes.erase(first, last);
      nodes.insert(it, plan.seq);
      replaceGeneration += 1;
      return plan.replace.replacement;
    }

    case SurfacePlan::Kind::None:
    default:
      return std::nullopt;
  }
}

}  // namespace

// ===========================================================================
// 全量折叠
// ===========================================================================

SurfaceFoldResult foldSurface(const std::vector<SessionEvent>& events) {
  SurfaceFoldResult result;
  size_t replaceGeneration = 0;
  for (size_t index = 0; index < events.size(); ++index) {
    const SurfacePlan plan =
        planSurfaceEvent(result.nodes, events[index], index, events);
    auto replacement =
        applySurfacePlan(result.nodes, replaceGeneration, plan);
    if (replacement.has_value()) {
      result.replacements.push_back(std::move(*replacement));
    }
  }
  return result;
}

// ===========================================================================
// SurfaceManager
// ===========================================================================

const std::vector<size_t>& SurfaceManager::nodes() {
  processDelta();
  return nodesState;
}

size_t SurfaceManager::replaceGeneration() {
  processDelta();
  return replaceGenerationState;
}

void SurfaceManager::processDelta() {
  while (processedCount < eventLog.size()) {
    const SurfacePlan plan = planSurfaceEvent(nodesState, eventLog[processedCount],
                                              processedCount, eventLog);
    applySurfacePlan(nodesState, replaceGenerationState, plan);
    ++processedCount;
  }
}

void SurfaceManager::validateNext(const SessionEvent& event,
                                  size_t expectedSeq) {
  // 先追平已入库的部分 —— 候选的区间校验必须对着「它将要看到的那个表面」做。
  processDelta();
  // 只规划、不提交。提交交给下一次 processDelta() 从日志追平。
  //
  //
  // 为什么不在这里就提交: 校验通过与事件入库之间存在一个窗口 (调用方在两者之间还要做
  // 别的事, 那些事可能失败)。若在此提交, 一次「校验通过但未入库」就会让表面状态比日志
  // 多一个节点, 而 processDelta 的追平条件是「已处理数 < 日志长度」, 永远不会自行修复。
  // 代价是同一条 surface 事件被规划两次, 对少数派的 surface 事件无实质开销。
  (void)planSurfaceEvent(nodesState, event, expectedSeq, eventLog);
}

}
