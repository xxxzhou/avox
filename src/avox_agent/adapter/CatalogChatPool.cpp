#include "CatalogChatPool.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

#include "avox/module/LogHelper.hpp"

namespace avox {

// 候选列表即装配期从目录里按序提取的模型名 (配置的模型在首位)。
CatalogChatPool::CatalogChatPool(std::vector<std::string> candidates)
    : ids(std::move(candidates)) {}

// 本地目录池不使用自动刷新 (候选来自 providers.json, 装配期已装载)。
bool CatalogChatPool::stale() const { return false; }

void CatalogChatPool::refresh() {}

size_t CatalogChatPool::count() const { return ids.size(); }

std::string CatalogChatPool::pickNext() {
  const auto now = std::chrono::steady_clock::now();
  // 惰性清除已过期的冷却项, 让冷却结束的模型重新可被选中。
  cooldown.erase(std::remove_if(cooldown.begin(), cooldown.end(),
                                [now](const auto& item) {
                                  return item.second <= now;
                                }),
                 cooldown.end());
  if (ids.empty()) return {};

  for (size_t i = 0; i < ids.size(); ++i) {
    const size_t idx = (cursor + i) % ids.size();
    const std::string& model = ids[idx];
    const bool cooling = std::any_of(cooldown.begin(), cooldown.end(),
                                     [&model](const auto& item) {
                                       return item.first == model;
                                     });
    if (!cooling) {
      cursor = (idx + 1) % ids.size();
      return model;
    }
  }
  // 全部都在冷却 (限流风暴): 返回空, 由路由策略决定兜底/保持原样。
  return {};
}

void CatalogChatPool::markRateLimited(const std::string& model) {
  if (model.empty()) return;
  const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(60);
  auto found = std::find_if(cooldown.begin(), cooldown.end(),
                            [&model](const auto& item) {
                              return item.first == model;
                            });
  if (found != cooldown.end()) {
    found->second = until;
  } else {
    cooldown.emplace_back(model, until);
  }
}

}