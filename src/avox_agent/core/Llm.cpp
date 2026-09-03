#include "Llm.hpp"

#include <stdexcept>

namespace avox {

size_t BlockAssembler::slot(int index) {
  const auto it = slotOf.find(index);
  if (it != slotOf.end()) return it->second;
  // 占位块立刻被随后的 delta 或 block-end 改写, 类型不会说谎。
  assembled.emplace_back(TextBlock{});
  slotOf.emplace(index, assembled.size() - 1);
  return assembled.size() - 1;
}

void BlockAssembler::push(const StreamChunk& chunk) {
  // 记账与结束分片不属于消息内容, 只进日志。
  if (std::holds_alternative<StreamUsage>(chunk)) return;
  if (std::holds_alternative<StreamFinish>(chunk)) return;

  if (const auto* start = std::get_if<StreamBlockStart>(&chunk)) {
    // 重复的 start 忽略; 未知 blockType 无法用 ContentBlock 表达 —— 模型输出流的
    // 词汇只有 text/reasoning/tool-call, 出现别的就是需要移植语义的信号, 响亮失败。
    if (slotOf.count(start->index) != 0) return;
    if (start->blockType == "text") {
      slotOf.emplace(start->index, assembled.size());
      assembled.emplace_back(TextBlock{});
    } else if (start->blockType == "reasoning") {
      slotOf.emplace(start->index, assembled.size());
      assembled.emplace_back(ReasoningBlock{});
    } else if (start->blockType == "tool-call") {
      slotOf.emplace(start->index, assembled.size());
      assembled.emplace_back(ToolCallBlock{});
    } else {
      throw std::runtime_error("流分片 block-start 的 blockType 不受支持: "
                               + start->blockType);
    }
    return;
  }

  if (const auto* text = std::get_if<StreamTextDelta>(&chunk)) {
    if (text->text.empty()) return;
    ContentBlock& block = assembled[slot(text->index)];
    if (auto* target = std::get_if<TextBlock>(&block)) {
      target->text += text->text;
    } else {
      block = TextBlock{text->text};
    }
    return;
  }

  if (const auto* reasoning = std::get_if<StreamReasoningDelta>(&chunk)) {
    if (reasoning->text.empty()) return;
    ContentBlock& block = assembled[slot(reasoning->index)];
    if (auto* target = std::get_if<ReasoningBlock>(&block)) {
      target->text += reasoning->text;
    } else {
      block = ReasoningBlock{reasoning->text};
    }
    return;
  }

  if (const auto* call = std::get_if<StreamToolCallDelta>(&chunk)) {
    ContentBlock& block = assembled[slot(call->index)];
    if (auto* target = std::get_if<ToolCallBlock>(&block)) {
      target->id = call->id;
      // 名字只在首片给, 后续片省略 —— 有了才覆盖。
      if (call->name.has_value()) target->name = *call->name;
      target->arguments += call->argumentsDelta;
    } else {
      block = ToolCallBlock{call->id, call->name.value_or(std::string()),
                            call->argumentsDelta};
    }
    return;
  }

  if (const auto* end = std::get_if<StreamBlockEnd>(&chunk)) {
    // block-end 自带装配完成的块, 以它为准 (dsh 协议: 终片是权威定稿)。
    const auto it = slotOf.find(end->index);
    if (it == slotOf.end()) {
      slotOf.emplace(end->index, assembled.size());
      assembled.push_back(end->block);
    } else {
      assembled[it->second] = end->block;
    }
    return;
  }
}

bool BlockAssembler::hasToolCalls() const {
  for (const ContentBlock& block : assembled) {
    if (std::holds_alternative<ToolCallBlock>(block)) return true;
  }
  return false;
}

std::vector<ToolCallBlock> BlockAssembler::toolCalls() const {
  std::vector<ToolCallBlock> calls;
  for (const ContentBlock& block : assembled) {
    if (const auto* call = std::get_if<ToolCallBlock>(&block)) {
      calls.push_back(*call);
    }
  }
  return calls;
}

std::vector<ContentBlock> BlockAssembler::interruptedBlocks() const {
  // text/reasoning 的非空白块照搬 (闭没闭合都一样: 未闭合的槽里已累积了 delta);
  // tool-call 从未分派, 不进被打断的前缀。
  const auto blank = [](const std::string& text) {
    return text.find_first_not_of(" \t\r\n\f\v") == std::string::npos;
  };
  std::vector<ContentBlock> blocks;
  for (const ContentBlock& block : assembled) {
    if (const auto* text = std::get_if<TextBlock>(&block)) {
      if (!blank(text->text)) blocks.push_back(block);
    } else if (const auto* reason = std::get_if<ReasoningBlock>(&block)) {
      if (!blank(reason->text)) blocks.push_back(block);
    }
  }
  return blocks;
}

}
