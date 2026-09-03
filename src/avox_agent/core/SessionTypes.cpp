#include "SessionTypes.hpp"

#include <array>
#include <utility>

namespace avox {

namespace {

// 事件类型 <-> wire 名的单一真相表。
//
// 名字与 dsh SessionEventMap 的键逐字一致 —— 这样两边的日志、fixture 与排查工具可以
// 直接对照, 也让后期跟随 dsh 演进时的差异一眼可见。
//
// 顺序必须与 EventType 枚举一致 (eventTypeName 直接按下标取)。
constexpr std::array<const char*, 26> kEventTypeNames = {
    "turn/start",
    "turn/end",
    "step/start",
    "step/end",
    "user/message",
    "assistant/chunk",
    "assistant/message",
    "tool/call",
    "tool/result",
    "request/header",
    "request/context",
    "agent/inbox/spliced",
    "session/end-seed",
    "compaction/start",
    "compaction/summary",
    "compaction/end",
    "approval/policy",
    "approval/asked",
    "approval/decided",
    "todo/write",
    "subagent/descriptor",
    // Agent Teams (dsh experimental agent-team 的 SessionEventMap 键, 逐字一致)。
    "team/member",
    "team/task",
    "team/message/queued",
    "team/message/delivered",
    // Opaque 不是 wire 名: 墓碑事件的真实类型名存在 OpaqueEventData::typeName 里,
    // eventTypeName 对它只给一个诊断占位符。fromEventTypeName 永不产出 Opaque ——
    // 墓碑只由装载层的白名单路径构造。
    "(opaque)",
};

static_assert(kEventTypeNames.size() == std::variant_size_v<EventData>,
              "事件名表与 EventData 变体数不符: 新增事件类型时两处必须同步");

}  // namespace

const char* eventTypeName(EventType type) {
  const size_t index = static_cast<size_t>(type);
  // 越界只可能来自把任意整数强转成 EventType (非本模块产出的值), 给一个可辨识的名字
  // 而不是越界读。
  if (index >= kEventTypeNames.size()) return "unknown";
  return kEventTypeNames[index];
}

std::optional<EventType> fromEventTypeName(const std::string& name) {
  for (size_t i = 0; i < kEventTypeNames.size(); ++i) {
    if (name == kEventTypeNames[i]) return static_cast<EventType>(i);
  }
  return std::nullopt;
}

EventType eventTypeOf(const EventData& data) {
  // EventType 与 EventData 的变体顺序一致 (由 SessionTypes.hpp 的 static_assert
  // 与本文件的名表 static_assert 共同把守), 故下标可直接转。
  return static_cast<EventType>(data.index());
}

// dsh 独有事件的白名单 (= dsh KNOWN_SESSION_EVENT_TYPES \ avox 的 21 种, 共 23 种)。
//
// 入选标准: dsh 装载校验里只有 user/message、assistant/message、tool/result 三类
// 可携带 surface 元数据, 这 23 种都不在其中 —— 跳过语义不改变 surface 投影, 模型
// 可见历史保持正确。原始行进墓碑, avox 重写日志时逐字节回放, dsh 侧续跑语义不丢。
//
// 已知语义损失 (第一期明确接受, 注释里留痕):
//   - plan/mode、sandbox/mode、permission/preset: 影响续跑的策略姿态。avox 没有对应
//     概念, resume 后按 avox 自己的策略装配走; dsh 拿回日志续跑时由它自己的折叠恢复。
//   - command/*、tool-workflow/*: 驱动层编排状态, avox 的 loop 不消费。
//   - session/title*: 展示元数据。
constexpr const char* kDshLogOnlyEventTypes[] = {
    // 会话组合选择 (展示/装配元数据, 不上 surface)。
    "agent-preset/selected",
    // 外部命令执行的开始与结束 (编排层记账)。
    "command/run",
    "command/done",
    // 工具结果裁剪的 shadow-price 记账: 只记日志, 紧随的 tool/result (replace) 才是
    // 真正的表面变更 —— 那条 avox 原生解码, 投影正确; 本事件进墓碑不影响任何语义。
    "compaction/prune",
    // 用户反馈记录。
    "feedback/record",
    // 目标变更。
    "goal/change",
    // hook 执行记录。
    "hook/invoked",
    "hook/result",
    // LLM 请求重试。
    "llm/retry",
    "llm/retry-started",
    // 权限基线覆盖 (已知语义损失, 见上)。
    "permission/preset",
    // 计划模式切换 (已知语义损失, 见上)。
    "plan/mode",
    // 沙箱模式切换 (已知语义损失, 见上)。
    "sandbox/mode",
    // 定时任务变更。
    "schedule/change",
    // 会话标题及其 LLM 请求 (展示元数据)。
    "session/title",
    "session/title-llm-request",
    // 工具工作流编排 (agent/run 两级)。
    "tool-workflow/agent-start",
    "tool-workflow/agent-end",
    "tool-workflow/run-start",
    "tool-workflow/run-end",
    // 代码派发工具 (dsh 内建工具的实现细节记账)。
    "tool/code-dispatch",
    "tool/code-dispatch-start",
    // web 搜索的 LLM 请求记账。
    "web/deepseek-search-llm-request",
};

bool isDshLogOnlyEventTypeName(const std::string& name) {
  for (const char* known : kDshLogOnlyEventTypes) {
    if (name == known) return true;
  }
  return false;
}

}
