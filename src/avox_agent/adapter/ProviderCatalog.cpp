#include "ProviderCatalog.hpp"

#include <algorithm>
#include <stdexcept>

namespace avox {

namespace {

bool has(const Json& j, const char* key) { return j.bObject() && j.find(key); }

std::string readString(const Json& j, const char* key, const std::string& fallback) {
  if (!has(j, key)) return fallback;
  const Json& v = j[key];
  if (!v.bString()) throw std::runtime_error(std::string("配置 ") + key + " 必须是字符串");
  return v.get<std::string>();
}

int readInt(const Json& j, const char* key, int fallback) {
  if (!has(j, key)) return fallback;
  const Json& v = j[key];
  if (!v.bInt()) throw std::runtime_error(std::string("配置 ") + key + " 必须是整数");
  return static_cast<int>(v.get<int64_t>());
}

bool readBool(const Json& j, const char* key, bool fallback) {
  if (!has(j, key)) return fallback;
  const Json& v = j[key];
  if (!v.bBool()) throw std::runtime_error(std::string("配置 ") + key + " 必须是布尔");
  return v.get<bool>();
}

int64_t readInt64(const Json& j, const char* key, int64_t fallback) {
  if (!has(j, key)) return fallback;
  const Json& v = j[key];
  if (!v.bInt()) throw std::runtime_error(std::string("配置 ") + key + " 必须是整数");
  return v.get<int64_t>();
}

double readDouble(const Json& j, const char* key, double fallback) {
  if (!has(j, key)) return fallback;
  const Json& v = j[key];
  if (v.bInt()) return static_cast<double>(v.get<int64_t>());
  if (!v.bNumber()) throw std::runtime_error(std::string("配置 ") + key + " 必须是数字");
  return v.get<double>();
}

// 基地址: 规范字段 apiUrl, 兼容旧写法 url。返回是否取到字符串。
std::string readUrl(const Json& j, const std::string& fallback) {
  if (has(j, "apiUrl") && j["apiUrl"].bString()) return j["apiUrl"].get<std::string>();
  if (has(j, "url") && j["url"].bString()) return j["url"].get<std::string>();
  return fallback;
}

// 节点是否携带基地址 (apiUrl 或旧 url)。
bool hasUrl(const Json& j) {
  return has(j, "apiUrl") || has(j, "url");
}

// apiKey 是否是特殊占位符 (待申请)。占位形态: YOUR_ 前缀 或 含 < / >。
bool isPlaceholderKey(const std::string& key) {
  return key.empty() || key.rfind("YOUR_", 0) == 0 || key.find('<') != std::string::npos;
}

// 智谱视觉模型多为单图, 自动标成单图, 避免多图端点拒绝。
void clampMultiImage(const std::string& model, bool& multiImage) {
  if (multiImage && model.rfind("glm-4v", 0) == 0) multiImage = false;
}

// 从模型节点把能力/计量字段填进条目; 端点字段 (apiUrl/apiPath/apiKey) 由厂商默认 + 模型覆盖。
void parseModel(ProviderEntry& provider, const Json& modelNode,
                const std::string& modelName) {
  provider.model = modelName;
  provider.chat = readBool(modelNode, "chat", false);
  provider.imageInput = readBool(modelNode, "imageInput", false);
  provider.imageOutput = readBool(modelNode, "imageOutput", false);
  provider.multiImage = readBool(modelNode, "multiImage", true);
  clampMultiImage(provider.model, provider.multiImage);
  provider.contextWindow = readInt64(modelNode, "contextWindow", 0);
  // 模型级端点覆盖 (如 cogview 的 apiPath 不同)。
  if (modelNode.find("apiPath") && modelNode["apiPath"].bString())
    provider.apiPath = readString(modelNode, "apiPath", provider.apiPath);
  if (modelNode.find("apiKey") && modelNode["apiKey"].bString())
    provider.apiKey = readString(modelNode, "apiKey", provider.apiKey);
  if (modelNode.bObject() && hasUrl(modelNode)) provider.apiUrl = readUrl(modelNode, provider.apiUrl);
  if (modelNode.bObject() && modelNode.find("tokenMeter") &&
      modelNode["tokenMeter"].bObject()) {
    const Json& tm = modelNode["tokenMeter"];
    provider.tokenMeter.enabled =
        readBool(tm, "enabled", provider.tokenMeter.enabled);
    provider.tokenMeter.inputPricePerMillion = readDouble(
        tm, "inputPricePerMillion", provider.tokenMeter.inputPricePerMillion);
    provider.tokenMeter.cachedInputPricePerMillion = readDouble(
        tm, "cachedInputPricePerMillion",
        provider.tokenMeter.cachedInputPricePerMillion);
    provider.tokenMeter.outputPricePerMillion = readDouble(
        tm, "outputPricePerMillion", provider.tokenMeter.outputPricePerMillion);
    provider.tokenMeter.creditsPerYuan = readDouble(
        tm, "creditsPerYuan", provider.tokenMeter.creditsPerYuan);
  }
}

// 解析一个模型条目, 兼容两种形态:
//   新式两层: 键 == 厂商, 值含 "models" 子对象, 每个模型一个条目, 厂商级字段 (apiUrl/apiKey/free) 共用。
//   旧式扁平: 键 == 模型实例 (含 apiUrl), 值直接是一个模型配置。
void parseEntry(std::vector<ProviderEntry>& list, const std::string& key,
                const Json& node, int defaultTimeout) {
  const bool layered =
      node.bObject() && node.find("models") && node["models"].bObject();
  if (layered) {
    const std::string vendor = key;
    const Json::JsonObject& models = node["models"].get<Json::JsonObject>();
    for (const auto& m : models) {
      ProviderEntry provider;
      provider.provider = vendor;
      provider.name = vendor + "." + m.first;
      provider.free = readBool(node, "free", false);
      provider.apiUrl = readUrl(node, "");
      provider.apiPath = readString(node, "apiPath", provider.apiPath);
      provider.apiKey = readString(node, "apiKey", "");
      provider.timeoutSec = readInt(node, "timeoutSec", defaultTimeout);
      if (provider.timeoutSec < 1) provider.timeoutSec = 180;
      parseModel(provider, m.second, m.first);
      list.push_back(std::move(provider));
    }
    return;
  }
  // 旧式扁平: 节点本身即一个模型配置。
  ProviderEntry provider;
  provider.provider = key;
  provider.name = key;
  if (node.bObject()) {
    provider.free = readBool(node, "free", false);
    provider.apiUrl = readUrl(node, "");
    provider.apiPath = readString(node, "apiPath", provider.apiPath);
    provider.apiKey = readString(node, "apiKey", "");
    provider.timeoutSec = readInt(node, "timeoutSec", defaultTimeout);
    if (provider.timeoutSec < 1) provider.timeoutSec = 180;
  }
  const size_t pos = key.find('.');
  const std::string modelName =
      pos == std::string::npos ? std::string() : key.substr(pos + 1);
  if (!modelName.empty()) {
    parseModel(provider, node, modelName);
  } else {
    provider.model = readString(node, "model", "");
  }
  list.push_back(std::move(provider));
}

// 把节点放进列表; 若已存在同 apiUrl+model 则跳过 (去重)。
void pushUnique(std::vector<ProviderEntry>& list, const ProviderEntry& provider) {
  for (const ProviderEntry& exist : list) {
    if (exist.id() == provider.id()) return;
  }
  list.push_back(provider);
}

// 把偏好指定的节点提到列表最前 (保持其余相对顺序)。
void liftPinned(std::vector<ProviderEntry>& list,
                const std::vector<std::string>& pinned) {
  for (size_t i = pinned.size(); i > 0; --i) {
    const std::string& wanted = pinned[i - 1];
    for (size_t k = 0; k < list.size(); ++k) {
      if (list[k].name == wanted) {
        ProviderEntry p = list[k];
        list.erase(list.begin() + static_cast<long>(k));
        list.insert(list.begin(), std::move(p));
        break;
      }
    }
  }
}

}  // namespace

bool ProviderEntry::needsApiKey() const { return isPlaceholderKey(apiKey); }

std::string ProviderEntry::apiKeyHint() const {
  if (!needsApiKey()) return std::string();
  const std::string label = name.empty() ? apiUrl : name;
  std::string platform = "开放平台";
  if (apiUrl.find("bigmodel") != std::string::npos) platform = "智谱开放平台 (bigmodel.cn)";
  else if (apiUrl.find("openrouter") != std::string::npos) platform = "OpenRouter (openrouter.ai)";
  else if (apiUrl.find("opencode") != std::string::npos) platform = "OpenCodeZen (opencode.ai)";
  return "供应商 \"" + label + "\" 需要有效 API Key: 请到 " + platform
         + " 申请后, 替换 agent.json 对应节点里的占位符。";
}

bool ProviderEntry::ready() const {
  return !apiUrl.empty() && !apiPath.empty() && !model.empty() && !needsApiKey();
}

void ProviderCatalog::load(const Json& root) {
  catalog.clear();
  inputList.clear();
  genList.clear();
  inputPinned.clear();
  outputPinned.clear();

  // 偏好与超时: 顶层字段 (providers.json) + 兼容旧式 vision 开关。
  explicitVision = false;
  enabledFlag = false;
  if (root.bObject()) {
    timeoutSec = readInt(root, "timeoutSec", timeoutSec);
    if (timeoutSec < 1) timeoutSec = 180;
    const std::string inModel = readString(root, "inputModel", "");
    const std::string outModel = readString(root, "outputModel", "");
    if (!inModel.empty()) inputPinned.push_back(inModel);
    if (!outModel.empty()) outputPinned.push_back(outModel);
    if (has(root, "vision") && root["vision"].bObject()) {
      const Json& vision = root["vision"];
      explicitVision = true;
      enabledFlag = readBool(vision, "enabled", false);
      const std::string vi = readString(vision, "inputModel", "");
      const std::string vo = readString(vision, "outputModel", "");
      if (!vi.empty()) inputPinned.push_back(vi);
      if (!vo.empty()) outputPinned.push_back(vo);
    }
  }

  // 收集全部模型节点: 来自 providers 子对象 (新 providers.json), 兼容旧式扁平顶层。
  const Json* source = &root;
  if (root.bObject() && has(root, "providers") && root["providers"].bObject()) {
    source = &root["providers"];
  }
  if (source->bObject()) {
    const Json::JsonObject& object = source->get<Json::JsonObject>();
    for (const auto& entry : object) {
      // 保留规范: 顶层带 apiUrl 的模型配置, 以及顶层带 models 的两层厂商目录。
      if (entry.second.bObject() && !entry.second.find("models")) {
        // 旧式扁平模型配置 (或 vision 策略节点): 需含基地址 (apiUrl/url) 才收。
        if (entry.first == "vision" || !hasUrl(entry.second)) continue;
      }
      parseEntry(catalog, entry.first, entry.second, timeoutSec);
    }
  }

  // 一个模型节点都没有: 自动填充内建免费节点, 并把视觉能力默认开 (零配置即可试)。
  if (catalog.empty()) {
    catalog = defaultFreeProviders();
    if (!explicitVision) enabledFlag = true;
  }

  // 推断视觉开关: 显式写了就用显式值; 否则看是否存在 imageInput 节点。
  if (!explicitVision) {
    enabledFlag = false;
    for (const ProviderEntry& provider : catalog) {
      if (provider.imageInput) { enabledFlag = true; break; }
    }
  }

  // 按能力整理供应商列表 (去重 + 偏好置顶)。
  for (const ProviderEntry& provider : catalog) {
    if (provider.imageInput) pushUnique(inputList, provider);
    if (provider.imageOutput) pushUnique(genList, provider);
  }
  liftPinned(inputList, inputPinned);
  liftPinned(genList, outputPinned);

  // 启用但没有图像理解节点: 兜底一个 anionex 免费端点 (公开密钥, 开箱即用)。
  if (enabledFlag && inputList.empty()) {
    ProviderEntry fallback;
    fallback.name = "anionex";
    fallback.free = true;
    fallback.imageInput = true;
    fallback.apiUrl = "https://vision.anionex.me";
    fallback.apiPath = "/v1/chat/completions";
    fallback.apiKey = "https://agent-vision.anionex.me";
    fallback.model = "gemini-3.7-flash";
    fallback.timeoutSec = timeoutSec;
    fallback.multiImage = false;
    inputList.push_back(std::move(fallback));
  }
}

bool ProviderCatalog::hasPendingKeys() const {
  for (const ProviderEntry& provider : catalog) {
    if (provider.needsApiKey()) return true;
  }
  return false;
}

std::vector<std::string> ProviderCatalog::apiKeyHints() const {
  std::vector<std::string> hints;
  for (const ProviderEntry& provider : catalog) {
    const std::string hint = provider.apiKeyHint();
    if (!hint.empty()) hints.push_back(hint);
  }
  return hints;
}

std::vector<ProviderEntry> defaultFreeProviders() {
  std::vector<ProviderEntry> list;

  // 智谱文本 (glm-4.7-flash, 免费): 需 apikey 占位。
  ProviderEntry text;
  text.name = "zhipu-text";
  text.provider = "zhipu-free";
  text.free = true;
  text.chat = true;
  text.apiUrl = "https://open.bigmodel.cn/api/paas/v4";
  text.apiPath = "/chat/completions";
  text.apiKey = "YOUR_ZHIPU_API_KEY";
  text.model = "glm-4.7-flash";
  text.contextWindow = 204800;  // 200K precision (官方 200K)
  list.push_back(std::move(text));

  // 智谱文本 (glm-4-flash, 免费): 需 apikey 占位。与 glm-4.7-flash 同厂商免费聊天,
  // 让 429 限流时 CatalogChatPool 可自动同厂商轮换到它。
  ProviderEntry textAlt;
  textAlt.name = "zhipu-text-alt";
  textAlt.provider = "zhipu-free";
  textAlt.free = true;
  textAlt.chat = true;
  textAlt.apiUrl = "https://open.bigmodel.cn/api/paas/v4";
  textAlt.apiPath = "/chat/completions";
  textAlt.apiKey = "YOUR_ZHIPU_API_KEY";
  textAlt.model = "glm-4-flash";
  textAlt.contextWindow = 131072;  // 128K (官方 128K)
  list.push_back(std::move(textAlt));

  // anionex 视觉 (公开密钥, 开箱即用)。
  ProviderEntry vision;
  vision.name = "anionex";
  vision.provider = "anionex";
  vision.free = true;
  vision.imageInput = true;
  vision.apiUrl = "https://vision.anionex.me";
  vision.apiPath = "/v1/chat/completions";
  vision.apiKey = "https://agent-vision.anionex.me";
  vision.model = "gemini-3.7-flash";
  vision.multiImage = false;
  list.push_back(std::move(vision));

  // 智谱视觉 (glm-4v-flash, 免费): 需 apikey 占位; 单图。既能看图也做对话 (chat)。
  ProviderEntry zhipuVision;
  zhipuVision.name = "zhipu-vision";
  zhipuVision.provider = "zhipu-free";
  zhipuVision.free = true;
  zhipuVision.chat = true;
  zhipuVision.imageInput = true;
  zhipuVision.apiUrl = "https://open.bigmodel.cn/api/paas/v4";
  zhipuVision.apiPath = "/chat/completions";
  zhipuVision.apiKey = "YOUR_ZHIPU_API_KEY";
  zhipuVision.model = "glm-4v-flash";
  zhipuVision.contextWindow = 16384;  // 16K precision (官方 16K)
  zhipuVision.multiImage = false;
  list.push_back(std::move(zhipuVision));

  // 智谱视觉 (glm-4.6v-flash, 免费): 需 apikey 占位; 支持多图。既能看图也做对话 (chat)。
  ProviderEntry zhipuVision46;
  zhipuVision46.name = "zhipu-vision-46";
  zhipuVision46.provider = "zhipu-free";
  zhipuVision46.free = true;
  zhipuVision46.chat = true;
  zhipuVision46.imageInput = true;
  zhipuVision46.apiUrl = "https://open.bigmodel.cn/api/paas/v4";
  zhipuVision46.apiPath = "/chat/completions";
  zhipuVision46.apiKey = "YOUR_ZHIPU_API_KEY";
  zhipuVision46.model = "glm-4.6v-flash";
  zhipuVision46.contextWindow = 131072;  // 128K precision (官方 128K)
  zhipuVision46.multiImage = true;
  list.push_back(std::move(zhipuVision46));

  // 智谱文生图 (cogview-3-flash, 免费): 需 apikey 占位。
  ProviderEntry gen;
  gen.name = "zhipu-cogview";
  gen.provider = "zhipu-free";
  gen.free = true;
  gen.imageOutput = true;
  gen.apiUrl = "https://open.bigmodel.cn/api/paas/v4";
  gen.apiPath = "/images/generations";
  gen.apiKey = "YOUR_ZHIPU_API_KEY";
  gen.model = "cogview-3-flash";
  list.push_back(std::move(gen));

  return list;
}

}