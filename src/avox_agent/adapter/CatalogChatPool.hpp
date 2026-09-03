#pragma once

// ============================================================================
// 把 providers.json 里「同一厂商的免费聊天模型」装成 ModelPool (静态目录轮换)。
//
// 与 FreeModelPool 的差别: 候选模型来自本地目录 (ProviderCatalog 的 free+chat 节点),
// 不需要 GET /models 拉取 —— 所以 stale() 恒 false、refresh() 空操作。用途: 让配置好的
// 固定模型 (如 glm-4.7-flash) 被 429 限流时, 自动同厂商切到下一个可用免费聊天模型。
// ============================================================================

#include <chrono>
#include <string>
#include <utility>
#include <vector>

#include "avox_agent/policy/ModelRoutePolicy.hpp"

namespace avox {

class CatalogChatPool : public ModelPool {
 public:
  // candidates: 某厂商的免费聊天模型条目 (只取其中的 model 名; 保持传入顺序)。
  explicit CatalogChatPool(std::vector<std::string> candidates);

  bool stale() const override;
  void refresh() override;
  size_t count() const override;
  std::string pickNext() override;
  void markRateLimited(const std::string& model) override;

 private:
  std::vector<std::string> ids;
  size_t cursor = 0;
  // id -> 冷却截止时刻 (pickNext 时惰性跳过过期项)。
  std::vector<std::pair<std::string, std::chrono::steady_clock::time_point>> cooldown;
};

}