#pragma once

// ============================================================================
// token-meter: 每轮积分计量。
//
// 对齐 dsh 的 packages/llm/token-meter。dsh 的 token-meter 是 cordis 插件, 做的是
// 「测量」——回放重算请求压力 + surface 启发式定价, 给 compaction 当占用判据。这里只
// 移植它最朴素、也最有运维价值的一块: 按轮汇总真实 usage (输入/缓存命中/输出), 并按
// 权重折算成积分, 挂 agent/turn-stopping 每轮落一条日志。
//
// 刻意不做: 预算限额与超限截断 (那是另一层策略, 等有真实花费约束再加)、压力定价、
// 投影折叠。保持最小。
// ============================================================================

#include <cstdint>

#include "avox_agent/core/Agent.hpp"
#include "avox_agent/core/Scope.hpp"
#include "avox_agent/core/Session.hpp"

namespace avox {

// 每轮积分的计费配置。
//
// 积分 = 实际花费, 按「1 积分 = 1 分钱 (0.01 元)」折算, 因此典型的一轮 agent turn
// (输入上万 token + 缓存命中 + 输出) 约为 1 积分。默认值即 DeepSeek v4-flash 官方单价
// (元/百万 tokens), 见 https://api-docs.deepseek.com/zh-cn/quick_start/pricing。
// 价格变动时改这里即可, 可调参数不允许硬编码在策略里。
struct TokenMeterConfig {
  double inputPricePerMillion = 1.0;         // 输入 (缓存未命中)
  double cachedInputPricePerMillion = 0.02;  // 输入 (缓存命中)
  double outputPricePerMillion = 2.0;        // 输出
  double creditsPerYuan = 100.0;             // 1 元 = 100 积分
};

// 一轮的用量汇总与积分。
struct TurnCredit {
  int turn = 0;
  int64_t inputTokens = 0;        // 未命中缓存的输入 token
  int64_t cachedInputTokens = 0;  // 命中缓存的输入 token
  int64_t outputTokens = 0;       // 输出 token
  double credits = 0.0;           // 按单价折算的实际花费 (分)
};

// 纯函数: 从会话日志汇总某一轮的用量并折算积分。测试友好, 不依赖扩展点。
TurnCredit aggregateTurnCredits(const Session& session, int turn,
                                const TokenMeterConfig& config);

// 纯函数: 汇总整个会话 (跨所有轮) 的用量与积分。turn 字段不填 (恒 0)。
// 供 shell 状态栏等消费方展示「当前会话累计」, 与 aggregateTurnCredits 共用价格折算。
TurnCredit aggregateSessionCredits(const Session& session,
                                   const TokenMeterConfig& config);

// 安装 token-meter: 每轮关闭前统计并记日志。返回撤销器。
Disposer installTokenMeter(AgentExtensionPoints& points,
                           const TokenMeterConfig& config);

}
