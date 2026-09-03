#include "DeploymentLoader.hpp"

#include <algorithm>
#include <filesystem>
#include <stdexcept>

#include "DshAttachmentStore.hpp"
#include "avox/module/AssetLoader.hpp"
#include "avox/module/Json.hpp"

namespace avox {

namespace {

bool has(const Json& node, const char* key) {
  return node.bObject() && node.find(key);
}

std::string readString(const Json& node, const char* key,
                      const std::string& fallback = std::string()) {
  if (!has(node, key)) return fallback;
  const Json& value = node[key];
  return value.bString() ? value.get<std::string>() : fallback;
}

int readInt(const Json& node, const char* key, int fallback) {
  if (!has(node, key)) return fallback;
  const Json& value = node[key];
  return value.bInt() ? static_cast<int>(value.get<int64_t>()) : fallback;
}

// 温度直接是小数 (dsh wire 上就是普通 JSON 数), 不再用 temperatureMilli 定点。
double readNumber(const Json& node, const char* key, double fallback) {
  if (!has(node, key)) return fallback;
  const Json& value = node[key];
  // Json 把整数与浮点分开存, 两种都认。
  if (value.bInt()) return static_cast<double>(value.get<int64_t>());
  if (value.bNumber()) return value.get<double>();
  return fallback;
}

bool readBool(const Json& node, const char* key, bool fallback) {
  if (!has(node, key)) return fallback;
  const Json& value = node[key];
  return value.bBool() ? value.get<bool>() : fallback;
}

// 每个大模型的积分规则不同 (单价、积分折算各异), 所以模型配置节内可放自己的 tokenMeter,
// 覆盖顶层兜底默认; 没写就沿用顶层 (或默认值)。enabled 与各价格字段逐项覆盖。
void applyTokenMeterOverride(AgentConfig& config, const Json& node) {
  if (!has(node, "tokenMeter")) return;
  const Json& tm = node["tokenMeter"];
  config.enableTokenMeter = readBool(tm, "enabled", config.enableTokenMeter);
  config.tokenMeter.inputPricePerMillion = readNumber(
      tm, "inputPricePerMillion", config.tokenMeter.inputPricePerMillion);
  config.tokenMeter.cachedInputPricePerMillion = readNumber(
      tm, "cachedInputPricePerMillion",
      config.tokenMeter.cachedInputPricePerMillion);
  config.tokenMeter.outputPricePerMillion = readNumber(
      tm, "outputPricePerMillion", config.tokenMeter.outputPricePerMillion);
  config.tokenMeter.creditsPerYuan =
      readNumber(tm, "creditsPerYuan", config.tokenMeter.creditsPerYuan);
}

// 按 provider 名从 config/providers.json 取厂商节点 (新式两层结构, 含 url/apiPath/apiKey/models)。
// 供主对话模型选择用: agent.json 只写 provider 名 + model, 端点/模型级细节由供应商目录提供。
Json resolveCatalogProvider(const std::string& name) {
  Json empty(Json::JsonObject{});
  const auto raw = AssetLoader::loadToMemory("config/providers.json");
  if (raw.empty()) return empty;
  Json root;
  try {
    root = parserJson(std::string(raw.begin(), raw.end()).c_str());
  } catch (...) {
    return empty;
  }
  if (!root.bObject() || !has(root, "providers") || !root["providers"].bObject()) return empty;
  const Json& provs = root["providers"];
  if (!provs.bObject() || !provs.find(name.c_str())) return empty;
  return provs[name.c_str()];
}

}  // namespace

AgentDeployment loadDeployment(const std::string& agentJson,
                              const std::string& dataRoot) {
  Json root;
  try {
    root = parserJson(agentJson.c_str());
  } catch (const std::exception& e) {
    throw std::runtime_error(std::string("agent.json 解析失败: ") + e.what());
  }
  if (!root.bObject()) throw std::runtime_error("agent.json 必须是 JSON 对象");

  AgentDeployment deployment;

  // agent.json 顶层即 AgentConfig (单份平铺): provider 是厂商名, model 是模型名。
  const Json& node = root;
  const std::string providerName = readString(node, "provider", "openai");
  const std::string nodeModel = readString(node, "model", "");
  // 端点细节: provider 名 → providers.json 的厂商节点 (新式两层); 找不到则报错。
  const Json catalogVendor = resolveCatalogProvider(providerName);
  const bool hasVendor = catalogVendor.bObject();
  // 模型级字段: vendor.models[nodeModel] (新式两层); 无则保持 Null。
  Json catalogModel;
  if (hasVendor && has(catalogVendor, "models") && catalogVendor["models"].bObject()) {
    const Json& models = catalogVendor["models"];
    if (has(models, nodeModel.c_str()) && models[nodeModel.c_str()].bObject()) {
      catalogModel = models[nodeModel.c_str()];
    }
  }
  const bool hasModel = catalogModel.bObject();
  const auto pickStr = [&](const char* key, const std::string& fb) {
    // 优先级: 模型级 > 厂商级。agent.json 顶层不再内联端点, 仅作最后兜底。
    if (hasModel) {
      const std::string vm = readString(catalogModel, key, "");
      if (!vm.empty()) return vm;
    }
    if (hasVendor) {
      const std::string vp = readString(catalogVendor, key, "");
      if (!vp.empty()) return vp;
    }
    return readString(node, key, fb);
  };
  deployment.llm.apiUrl = pickStr("apiUrl", "");
  if (deployment.llm.apiUrl.empty()) deployment.llm.apiUrl = pickStr("url", "");
  deployment.llm.apiPath = pickStr("apiPath", "/chat/completions");
  deployment.llm.apiKey = pickStr("apiKey", "");
  deployment.llm.providerName = providerName;
  // 模型名优先用 agent.json 显式 model; 厂商目录兜底。
  deployment.llm.model =
      !nodeModel.empty() ? nodeModel : pickStr("model", "");
  if (deployment.llm.apiUrl.empty()) {
    throw std::runtime_error("provider \"" + providerName
                            + "\" 未在 providers.json 找到端点 apiUrl");
  }

  // 采样参数与超时: agent.json 顶层直读。
  deployment.llm.temperature =
      static_cast<float>(readNumber(node, "temperature", 0.7));
  deployment.llm.maxTokens = readInt(node, "maxTokens", 16384);
  // 推理等级 (reasoning_effort, OpenAI 兼容 wire)。优先级: providers.json 模型级 >
  // 厂商级 > agent.json 顶层 > 内建 "medium" (o3/o4-mini/gpt-5/r1 的中间档, DSH 同款)。
  // 模型/厂商级显式写 "" = 该档不发字段 (非推理模型如 GPT-4o 不踩雷)。
  // 各厂商合法档位不一: zhipu 的 GLM-5.3 / 5.3-flash 强制思考且只认 low/high/max,
  // medium 直接 HTTP 400 (错误码 1210), 这类模型要在 providers.json 里显式配好。
  if (hasModel && has(catalogModel, "reasoningEffort")
      && catalogModel["reasoningEffort"].bString()) {
    deployment.llm.reasoningEffort = catalogModel["reasoningEffort"].get<std::string>();
  } else if (hasVendor && has(catalogVendor, "reasoningEffort")
             && catalogVendor["reasoningEffort"].bString()) {
    deployment.llm.reasoningEffort = catalogVendor["reasoningEffort"].get<std::string>();
  } else {
    deployment.llm.reasoningEffort = readString(node, "reasoningEffort", "medium");
  }
  deployment.llm.connTimeoutSec =
      readInt(node, "connTimeoutSeconds", 30);
  deployment.llm.readTimeoutSec =
      readInt(node, "readTimeoutSeconds", 300);
  deployment.llm.reasoningTimeoutSec =
      readInt(node, "reasoningTimeoutSeconds", 0);

  // 图片准入限额 (agent.json 顶层 "imageLimits"): 键与 dsh attachment Config 同义,
  // 未配置的字段沿用 dsh 默认值 (见 core/AttachmentStore.hpp)。
  if (has(node, "imageLimits") && node["imageLimits"].bObject()) {
    const Json& il = node["imageLimits"];
    deployment.imageLimits.maxImageBytes =
        static_cast<int64_t>(readNumber(il, "maxImageBytes",
                                        static_cast<double>(deployment.imageLimits.maxImageBytes)));
    deployment.imageLimits.maxImagePixels =
        static_cast<int64_t>(readNumber(il, "maxImagePixels",
                                        static_cast<double>(deployment.imageLimits.maxImagePixels)));
    deployment.imageLimits.maxImageDimension =
        static_cast<int64_t>(readNumber(il, "maxImageDimension",
                                        static_cast<double>(deployment.imageLimits.maxImageDimension)));
    deployment.imageLimits.maxImagesPerMessage =
        static_cast<int64_t>(readNumber(il, "maxImagesPerMessage",
                                        static_cast<double>(deployment.imageLimits.maxImagesPerMessage)));
    deployment.imageLimits.maxMessageImageBytes =
        static_cast<int64_t>(readNumber(il, "maxMessageImageBytes",
                                        static_cast<double>(deployment.imageLimits.maxMessageImageBytes)));
  }

  // ---- AgentConfig ----
  //
  // 复用 AgentConfig::fromJson 解析顶层: 那里已经有全部字段的校验与默认值, 在这里
  // 再抄一遍等于两处需要同步。把整个文件喂进去, 忽略未知键即可。
  //
  // 先把部署期默认路径写进 JSON 再交给 fromJson: 最小配置把 spillRoot/sessionRoot 留空,
  // fromJson 的跨字段校验 (开了溢出就得有目录) 会在这里的默认值生效前先抛错。把「按环境
  // 推导」的默认值在此落进 JSON, 校验与落盘就都对得上。
  if (root.bObject()) {
    if (has(root, "spill") && root["spill"].bObject()) {
      Json& spillNode = root["spill"];
      if (readString(spillNode, "spillRoot", "").empty()) {
        spillNode["spillRoot"] = (std::filesystem::path(dataRoot) / "spill").string();
      }
      if (readInt(spillNode, "maxInlineBytes", 0) <= 0) {
        spillNode["maxInlineBytes"] = 32768;
      }
    }
    // 会话根默认与 dsh 同一 home (<DSH_HOME>/sessions) —— 互访的前提是两边默认落同一
    // 目录树, dsh 写的会话 avox 才 resume 得到。显式配置仍优先。
    if (readString(root, "sessionRoot", "").empty()) {
      root["sessionRoot"] =
          (std::filesystem::path(resolveDshHome()) / "sessions").string();
    }
  }
  deployment.agent = AgentConfig::fromJson(root.dump());
  deployment.agent.provider = deployment.llm.providerName;
  deployment.agent.model = deployment.llm.model;
  deployment.agent.maxTokens = deployment.llm.maxTokens;

  // 每模型积分规则: 模型级 tokenMeter 作底座, 再让厂商级兜底, agent.json 节点内可逐项覆盖。
  if (hasModel) applyTokenMeterOverride(deployment.agent, catalogModel);
  if (hasVendor) applyTokenMeterOverride(deployment.agent, catalogVendor);
  applyTokenMeterOverride(deployment.agent, node);

  // 存储路径: 上面的部署期默认已写入 agent.json, 这里再兜底一次 (兼容直接构造的配置)。
  if (deployment.agent.sessionRoot.empty()) {
    deployment.agent.sessionRoot =
        (std::filesystem::path(resolveDshHome()) / "sessions").string();
  }
  if (deployment.agent.spill.spillRoot.empty()) {
    deployment.agent.spill.spillRoot =
        (std::filesystem::path(dataRoot) / "spill").string();
  }
  // 溢出裁剪默认开: 旧实现 (已删) 里模型面完全没有上限, 而 read / grep 正是最
  // 容易吐几 MB 的工具。32KB 是个保守起点 (够装完整的错误栈, 挡住整份日志)。
  if (deployment.agent.spill.maxInlineBytes <= 0) {
    deployment.agent.spill.maxInlineBytes = 32768;
  }

  // auto 模型: 打开模型路由策略。
  //
  // 现有 openrouter 配置的 model 就是 "auto", 旧实现在 SSE 循环里硬编码轮换; 新架构靠这个
  // 开关把它交给 ModelRoutePolicy。
  if (deployment.llm.model == deployment.agent.modelRoute.autoKeyword) {
    deployment.agent.enableModelRoute = true;
  }
  return deployment;
}

}
