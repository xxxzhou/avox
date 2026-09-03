#include "TokenMeter.hpp"

#include <cstdio>
#include <string>

#include "avox/module/LogHelper.hpp"

namespace avox {

namespace {

// 把积分格式化成可读的小数 (去掉多余尾零), 如 0.31 / 2.08 / 6.8。
std::string formatCredits(double credits) {
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%.4g", credits);
  return buffer;
}

// 累加一次 usage 到结果。
void accumulateUsage(TurnCredit& result, const TokenUsage& usage) {
  result.inputTokens += usage.inputTokens;
  result.cachedInputTokens += usage.cacheReadTokens.value_or(0);
  result.outputTokens += usage.outputTokens;
}

// 按单价折算积分 (元 = tokens * 单价(元/百万) / 1e6; 再按「1 元 = creditsPerYuan 积分」)。
void finalizeCredits(TurnCredit& result, const TokenMeterConfig& config) {
  const double costYuan =
      (config.inputPricePerMillion
           * static_cast<double>(result.inputTokens)
       + config.cachedInputPricePerMillion
           * static_cast<double>(result.cachedInputTokens)
       + config.outputPricePerMillion
           * static_cast<double>(result.outputTokens))
      / 1000000.0;
  result.credits = costYuan * config.creditsPerYuan;
}

}  // namespace

TurnCredit aggregateTurnCredits(const Session& session, int turn,
                                const TokenMeterConfig& config) {
  TurnCredit result;
  result.turn = turn;
  for (const SessionEvent& event : session.events()) {
    if (event.type != EventType::AssistantMessageEvent) continue;
    const auto& data = std::get<AssistantMessageData>(event.data);
    if (data.turn != turn) continue;
    if (!data.usage.has_value()) continue;
    accumulateUsage(result, *data.usage);
  }
  finalizeCredits(result, config);
  return result;
}

TurnCredit aggregateSessionCredits(const Session& session,
                                   const TokenMeterConfig& config) {
  TurnCredit result;
  for (const SessionEvent& event : session.events()) {
    if (event.type != EventType::AssistantMessageEvent) continue;
    const auto& data = std::get<AssistantMessageData>(event.data);
    if (!data.usage.has_value()) continue;
    accumulateUsage(result, *data.usage);
  }
  finalizeCredits(result, config);
  return result;
}

Disposer installTokenMeter(AgentExtensionPoints& points,
                           const TokenMeterConfig& config) {
  return points.turnStopping.on(
      [config](const TurnStoppingPayload& payload) {
        const TurnCredit credit =
            aggregateTurnCredits(payload.agent->session(), payload.turn, config);
        // 每轮都记: 没有 usage 的首轮 / 失败轮会打全 0, 那本身是有用的信息。
        LOGFLF(LogLevel::info, "[token-meter] turn ",
               std::to_string(payload.turn).c_str(), " 输入 ",
               std::to_string(credit.inputTokens).c_str(), " / 缓存命中 ",
               std::to_string(credit.cachedInputTokens).c_str(), " / 输出 ",
               std::to_string(credit.outputTokens).c_str(), " / 积分 ",
               formatCredits(credit.credits).c_str());
      });
}

}
