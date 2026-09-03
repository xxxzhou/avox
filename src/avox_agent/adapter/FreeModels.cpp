// FreeModels.cpp — 免费模型池 (过滤/排序/冷却/轮换, 纯逻辑无 HTTP)
// 来源: OpenRouter (pricing 判免费) / OpenCodeZen (-free 后缀判免费), 见 ModelPoolSource。
#include "FreeModels.hpp"

#include <algorithm>

#include "avox/module/Json.hpp"
#include "avox/module/LogHelper.hpp"

namespace avox {

namespace {
constexpr int kTtlSec = 3600;   // 模型列表缓存 1h

// ---- OpenRouter ----

// 家族权重 (好用 -> 不好用): id 命中关键词给分。可按实测调整偏好。
int openRouterWeight(const std::string& id) {
  if (id.find("gpt-oss") != std::string::npos) return 10;
  if (id.find("nemotron") != std::string::npos && id.find("reasoning") != std::string::npos) return 9;
  if (id.find("nemotron") != std::string::npos) return 8;
  if (id.find("deepseek") != std::string::npos) return 8;
  if (id.find("qwen") != std::string::npos) return 8;
  if (id.find("llama") != std::string::npos) return 8;
  if (id.find("gemma") != std::string::npos) return 7;
  if (id.find("mistral") != std::string::npos) return 7;
  if (id.find("openrouter/free") != std::string::npos) return 6;   // meta 路由
  if (id.find("cohere") != std::string::npos) return 5;
  if (id.find("ling") != std::string::npos || id.find("inclusionai") != std::string::npos) return 4;
  if (id.find("poolside") != std::string::npos || id.find("laguna") != std::string::npos) return 3;
  return 2;
}

// 排除非对话向 (内容安全 / 音乐生成)
bool excluded(const std::string& id) {
  return id.find("safety") != std::string::npos ||
         id.find("lyria") != std::string::npos;
}

// ---- OpenCodeZen ----

// Zen 的 /models 不带 pricing, 免费靠命名约定: id 以 -free 结尾。
// big-pickle 是官方限时免费的 stealth 模型, 无后缀, 单列。
bool zenFree(const std::string& id) {
  static const std::string kSuffix = "-free";
  return id == "big-pickle" ||
         (id.size() >= kSuffix.size() &&
          id.compare(id.size() - kSuffix.size(), kSuffix.size(), kSuffix) == 0);
}

// Zen 是 coding-agent 网关, 家族表按「写代码好不好用」排。免费集合小 (7 个上下),
// 权重只需维持家族间的相对次序。
int zenWeight(const std::string& id) {
  if (id.find("deepseek") != std::string::npos) return 10;   // coder 强项
  if (id == "big-pickle") return 9;                          // 官方主推免费入口 (stealth)
  if (id.find("nemotron") != std::string::npos) return 8;
  if (id.find("qwen") != std::string::npos) return 7;
  if (id.find("kimi") != std::string::npos) return 7;
  if (id.find("mimo") != std::string::npos) return 7;
  if (id.find("hy3") != std::string::npos) return 6;
  if (id.find("laguna") != std::string::npos) return 5;
  return 4;
}
}  // namespace

void FreeModels::load(const std::string& modelsJsonBody, ModelPoolSource source) {
  ranked.clear();
  Json root;
  try {
    root = parserJson(modelsJsonBody.c_str());
  } catch (...) {
    LOGFLF(LogLevel::warn, "FreeModels::load parse failed");
    return;
  }
  if (!root.bObject() || !root.find("data") || !root["data"].bArray()) {
    LOGFLF(LogLevel::warn, "FreeModels::load no data array");
    return;
  }
  for (size_t i = 0; i < root["data"].size(); i++) {
    const Json& m = root["data"][i];
    if (!m.bObject() || !m.find("id") || !m["id"].bString()) continue;
    std::string id = m["id"].get<std::string>();
    bool freeModel = false;
    if (source == ModelPoolSource::OpenCodeZen) {
      freeModel = zenFree(id);
    } else {
      // OpenRouter: 仅免费 (pricing.prompt == "0")
      if (m.find("pricing") && m["pricing"].bObject() &&
          m["pricing"].find("prompt") && m["pricing"]["prompt"].bString()) {
        freeModel = m["pricing"]["prompt"].get<std::string>() == "0";
      }
      if (excluded(id)) continue;
    }
    if (!freeModel) continue;
    FreeModel fm;
    fm.id = id;
    if (m.find("context_length") && m["context_length"].bInt()) {
      fm.contextLength = (int)m["context_length"].get<int64_t>();
    }
    fm.score = source == ModelPoolSource::OpenCodeZen ? zenWeight(id)
                                                      : openRouterWeight(id);
    ranked.push_back(std::move(fm));
  }
  // 排序: score 降序, 同分 context 降序
  std::sort(ranked.begin(), ranked.end(), [](const FreeModel& a, const FreeModel& b) {
    if (a.score != b.score) return a.score > b.score;
    return a.contextLength > b.contextLength;
  });
  loadTime = std::chrono::steady_clock::now();
  hasLoaded = true;
  LOGFLF(LogLevel::info, "FreeModels loaded ", ranked.size(), " free models; top=",
         (ranked.empty() ? std::string("(none)") : ranked[0].id).c_str());
}

bool FreeModels::stale() const {
  if (!hasLoaded || ranked.empty()) return true;
  auto age = std::chrono::duration_cast<std::chrono::seconds>(
      std::chrono::steady_clock::now() - loadTime).count();
  return age > kTtlSec;
}

std::string FreeModels::pickNext() {
  auto now = std::chrono::steady_clock::now();
  for (const auto& fm : ranked) {
    bool cooled = false;
    for (const auto& c : cooldown) {
      if (c.first == fm.id && c.second > now) { cooled = true; break; }
    }
    if (!cooled) return fm.id;
  }
  return "";
}

void FreeModels::markRateLimited(const std::string& id, int cooldownSec) {
  cooldown.emplace_back(id, std::chrono::steady_clock::now() + std::chrono::seconds(cooldownSec));
  auto now = std::chrono::steady_clock::now();
  cooldown.erase(std::remove_if(cooldown.begin(), cooldown.end(),
      [now](const std::pair<std::string, std::chrono::steady_clock::time_point>& c) {
        return c.second <= now;
      }), cooldown.end());
}

size_t FreeModels::count() const { return ranked.size(); }

std::string FreeModels::rankedDebug() const {
  std::string s;
  for (size_t i = 0; i < ranked.size(); i++) {
    if (i) s += ", ";
    s += ranked[i].id;
  }
  return s;
}

}
