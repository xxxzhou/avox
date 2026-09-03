#pragma once

// ============================================================================
// 把现有 FreeModels (免费模型池: OpenRouter / OpenCodeZen) 包成 ModelPool。
//
// FreeModels 本身是纯逻辑 (过滤/排序/冷却/轮换), 不含 HTTP —— 原来由 AgentClient 的
// fetchModelsBody 喂它。那段 HTTP 现在归本类, 于是「哪个模型可用」这件事完整收在一处,
// 而 ModelRoutePolicy 只管「什么时候换」。
// ============================================================================

#include <memory>
#include <string>

#include "avox_agent/adapter/FreeModels.hpp"
#include "avox_agent/policy/ModelRoutePolicy.hpp"

namespace avox {

class FreeModelPool : public ModelPool {
 public:
  // baseUrl: 形如 https://openrouter.ai 或 https://opencode.ai,
  // pathPrefix: 形如 /api/v1 或 /zen/v1 (GET <baseUrl><pathPrefix>/models)。
  FreeModelPool(std::string baseUrl, std::string pathPrefix, std::string apiKey,
                ModelPoolSource source = ModelPoolSource::OpenRouter);

  bool stale() const override;
  void refresh() override;
  size_t count() const override;
  std::string pickNext() override;
  void markRateLimited(const std::string& model) override;

  // 排序后的 id 列表 (供 shell 的 /status 显示)。
  std::string rankedDebug() const;

 private:
  std::string baseUrl;
  std::string pathPrefix;
  std::string apiKey;
  ModelPoolSource source;
  FreeModels models;
};

}
