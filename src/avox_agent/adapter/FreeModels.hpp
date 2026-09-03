#pragma once

#include <chrono>
#include <string>
#include <utility>
#include <vector>

#include "avox/AvoxDef.h"  // AVOX_START/END_NAMESPACE

namespace avox {

// 免费模型池的来源。两者的 /models 都是 OpenAI 风格 {"data":[{"id",...}]}, 差别在
// 「怎么判免费」与家族权重表:
//   OpenRouter: pricing.prompt == "0" (响应带 pricing); 家族多而杂 (含音乐/安全向)。
//   OpenCodeZen: 响应无 pricing, 免费靠命名约定 (id 以 -free 结尾); big-pickle 是
//     官方限时免费的 stealth 模型, 无后缀, 单列。免费模型全部走 /chat/completions
//     (OpenAI 兼容), 与 ChatProvider 路径吻合。
enum class ModelPoolSource {
  OpenRouter,
  OpenCodeZen,
};

// 免费模型池 (纯逻辑, 不含 HTTP)。
// FreeModelPool (adapter) 负责 GET /models 取 body 喂 load(); 本类只做 过滤/排序/冷却/轮换。
// 触发: config.apiUrl 含 openrouter.ai / opencode.ai 且 config.model == "auto"。
// 设计依据: 免费模型走 upstream 共享池, 时灵时不灵 (429 upstream rate-limited),
// 故按"家族权重 + 上下文"排好用->不好用, 哪个 429 就冷却并自动切下一个。
struct FreeModel {
  std::string id;
  int contextLength = 0;
  int score = 0;        // 家族权重 (上下文仅作同分 tiebreak; Zen 不报 context_length, 恒 0)
};

class FreeModels {
 public:
  // 解析 /models JSON body: 按来源过滤免费模型 + 排序, 缓存 (带 TTL)。
  void load(const std::string& modelsJsonBody,
            ModelPoolSource source = ModelPoolSource::OpenRouter);
  // 缓存是否需要刷新 (未加载 / 空 / 超 TTL)
  bool stale() const;
  // 取当前最优且未冷却的模型 id; 全冷却/空池返回 ""
  std::string pickNext();
  // 把某模型丢进冷却 (默认 60s); 顺手清过期项防膨胀
  void markRateLimited(const std::string& id, int cooldownSec = 60);
  size_t count() const;
  // 调试: 排序后的 id 列表
  std::string rankedDebug() const;

 private:
  std::vector<FreeModel> ranked;
  std::chrono::steady_clock::time_point loadTime;
  bool hasLoaded = false;
  // id -> 冷却截止时刻 (过期项在 markRateLimited/pickNext 时惰性清)
  std::vector<std::pair<std::string, std::chrono::steady_clock::time_point>> cooldown;
};

}
