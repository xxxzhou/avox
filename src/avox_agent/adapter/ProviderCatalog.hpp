#pragma once

// ============================================================================
// ProviderCatalog: 统一管理 agent.json 的模型供应商 —— 供应商配置的**唯一入口**。
//
// 抽象 "agent.json" 成一棵模型供应商目录树 (Catalog)。每个模型节点 (值是对象且含 url,
// 且键 != "vision") 用一个扁平能力 schema 描述:
//   free        是否免费
//   chat        支持文本对话
//   imageInput  支持图像理解 (视觉输入)
//   imageOutput 支持文生图 (图像输出)
//
// 于是 "Agent 能不能聊、能不能看图、能不能生图" 全部来自同一个配置文件; 运行时按能力
// 挑选供应商: 文本走 chat 节点, 图像理解走 inputProviders(), 文生图走 genProviders()。
// 当前默认供应商不支持某能力时就自动切到有能力且可用的节点 (失败切换见 VisionHttp)。
//
// **空目录自动填充**: 若 agent.json 没有任何模型节点, load 时自动填入一组内建**免费**
// 供应商 (智谱文本/视觉/文生图 + anionex 视觉), 保证零配置也能用。
//
// **占位密钥**: 需要 apikey 的节点用特殊占位符 (YOUR_ 前缀 / <占位>) 标记, 用户到开放
// 平台申请后替换; apiKeyHints() 返回申请指引, 供加载时提示。ProviderEntry::ready() 在
// 密钥仍为占位符时返回 false, 运行时不会把占位符当真密钥发出去。
//
// 归属 provider/adapter 域 (与 FreeModels、DeploymentLoader 同类); 视觉能力不再有
// VisionConfig/VisionProvider 一层的重复建模, 统一走本目录。
// ============================================================================

#include <string>
#include <vector>

#include "avox/module/Json.hpp"

namespace avox {

// 单个模型供应商的积分计量配置 (agent.json 节点内嵌 tokenMeter 节点; 顶层同结构值做默认)。
struct ProviderTokenMeter {
  bool enabled = false;
  double inputPricePerMillion = 0.0;
  double cachedInputPricePerMillion = 0.0;
  double outputPricePerMillion = 0.0;
  double creditsPerYuan = 0.0;
};

// 单个模型供应商条目。字段均驼峰/无下划线 (工程约定禁用下划线命名)。
struct ProviderEntry {
  // 厂商名 (providers.json 里 providers 下的键, 即 AgentConfig.provider)。模型名见 model。
  std::string provider;
  // 可读名 (通常是 provider+"."+model 组合标识)。内建免费节点用固定名。
  std::string name;
  bool free = false;
  bool chat = false;        // 支持文本对话
  bool imageInput = false;  // 支持图像理解
  bool imageOutput = false; // 支持文生图

  // 端点基地址 (对应 providers.json 的 apiUrl, 兼容旧字段 url)。
  std::string apiUrl;
  std::string apiPath = "/chat/completions";
  std::string apiKey;
  std::string model;
  int timeoutSec = 180;
  bool multiImage = true;

  // 窗口上下文 (tokens); 0 = 未配置/沿用默认 (agent.json 节点 contextWindow)。
  int64_t contextWindow = 0;
  // 该节点的积分计量 (价目/权重折算), 供 token-meter 等按模型计价。
  ProviderTokenMeter tokenMeter;

  // apiKey 为空或仍是占位符 (YOUR_ 前缀 / 含 <>) → 需要用户申请。
  bool needsApiKey() const;
  // apiKey 是占位符时的申请指引文案; 无需申请返回空串。
  std::string apiKeyHint() const;
  // 是否具备发起请求的完整配置 (基址 + 密钥 + 模型, 密钥非占位符)。
  bool ready() const;
  // 身份: url+model, 用于失败切换的冷却登记与去重。
  std::string id() const { return apiUrl + "/" + model; }
};

// 统一模型供应商目录。构造 providers.json → 收集/填充 → 派生各能力供应商列表。
class ProviderCatalog {
 public:
  // 从根 JSON 对象加载: 收集全部模型节点; 一个都没有则自动填充内建免费节点。
  // 同时按能力与偏好 (vision.inputModel/outputModel) 整理出 inputProviders/genProviders。
  void load(const Json& root);
  // 是否有任一模型节点 (含自动填充后的内建节点)。
  bool hasProviders() const { return !catalog.empty(); }
  const std::vector<ProviderEntry>& entries() const { return catalog; }

  // 视觉能力是否启用: 显式 vision.enabled 优先, 否则按是否有 imageInput 节点推断。
  bool enabled() const { return enabledFlag; }
  // 图像理解供应商 (imageInput 节点, 按偏好排序, 去重; 启用时兜底 anionex 免费端)。
  const std::vector<ProviderEntry>& inputProviders() const { return inputList; }
  // 文生图供应商 (imageOutput 节点, 按偏好排序, 去重)。
  const std::vector<ProviderEntry>& genProviders() const { return genList; }

  // 是否含需申请 apikey 的节点 (用于加载时提示)。
  bool hasPendingKeys() const;
  // 所有需申请 apikey 节点的申请指引文案。
  std::vector<std::string> apiKeyHints() const;

 private:
  std::vector<ProviderEntry> catalog;
  std::vector<ProviderEntry> inputList;
  std::vector<ProviderEntry> genList;
  bool explicitVision = false;  // agent.json 是否显式写了 vision 开关
  bool enabledFlag = false;
  std::vector<std::string> inputPinned;   // imageInput 偏好指定 (节点名)
  std::vector<std::string> outputPinned;  // imageOutput 偏好指定 (节点名)
  int timeoutSec = 180;
};

// 内建免费供应商集合 (agent.json 无任何模型节点时自动填充)。
std::vector<ProviderEntry> defaultFreeProviders();

}