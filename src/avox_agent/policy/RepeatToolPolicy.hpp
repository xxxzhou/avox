#pragma once

// ============================================================================
// 重复工具调用提醒 (循环卫生)。
//
// 对齐 dsh 的 packages/guard/repeat-tool-reminder。
//
// 防的具体故障: 模型拿同一组参数反复敲同一个工具打转。旧实现 (已删) 的工具循环是
// handleToolCallLoop -> handleSSEResponse 递归续发, **既没有步数上限也没有重复检测**,
// 模型反复 grep 同一个日志就无限跑, 只能靠人 Ctrl+C。
//
// 四个照搬的取舍:
//   1. 参数做**深度 key 排序**后比较 —— 属性顺序不同算同一次调用。
//   2. 计数放在 **post-execute** 而不是 pre: 被拒绝的调用也走这条链, 而模型狂敲一个被拒
//      的调用正是最该打断的循环。
//   3. **只观察和补充, 从不否决**: 先计数 (状态无条件推进) → next() 让下游仍可 block →
//      把提醒前置进 additionalContexts。
//   4. 用户插话后清链: 人工介入之后的重复不算打转。
// ============================================================================

#include <vector>

#include "avox_agent/core/Agent.hpp"
#include "avox_agent/core/Scope.hpp"
#include "avox_agent/core/ToolRuntime.hpp"

namespace avox {

struct RepeatToolPolicyConfig {
  // 触发提醒的次数档位。必须是递增的、去重的、且每个都 >= 2 的整数。
  //
  // 第一档发温和提醒, 之后发详细提醒 (工具名 + 次数 + 参数预览)。
  std::vector<int> thresholds{3, 5, 8};

  // 提醒里参数预览的字符上限。
  //
  // 只限制**模型可见文本**, 检测始终用完整的规范化串 —— 循环场景下一个写文件调用的正文
  // 会无界地骑进下一次请求, 那本身就是一种上下文爆炸。
  int argumentsPreviewChars = 500;
};

// 安装循环卫生策略; 返回撤销器 (合并了两处注册)。
//
// 配置非法 (空档位、非整数、小于 2、重复值、预览上限非正) 在安装时抛 —— 不做静默回退。
Disposer installRepeatToolPolicy(ToolRuntime& tools, AgentExtensionPoints& points,
                                RepeatToolPolicyConfig config);

}
