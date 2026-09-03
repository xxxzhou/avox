#include "DiagnosticAgent.hpp"

#include <filesystem>
#include <utility>

#include "avox/module/LogHelper.hpp"
#include "avox/module/AssetLoader.hpp"
#include "avox_agent/adapter/CatalogChatPool.hpp"
#include "avox_agent/adapter/ProviderCatalog.hpp"
#include "avox_agent/plugins/vision-toolkit/VisionTools.hpp"
#include "avox_agent/skills/SkillRegistry.hpp"
#include "avox_agent/tools/BuiltinTools.hpp"

namespace avox {

namespace {

// 从 https://host/api/v1 拆出 baseUrl 与 path 前缀 (模型池要用它拼 /models)。
void splitBaseUrl(const std::string& url, std::string& baseUrl,
                 std::string& pathPrefix) {
  baseUrl = url;
  pathPrefix.clear();
  const size_t schemeEnd = url.find("://");
  if (schemeEnd == std::string::npos) return;
  const size_t pathStart = url.find('/', schemeEnd + 3);
  if (pathStart == std::string::npos) return;
  baseUrl = url.substr(0, pathStart);
  pathPrefix = url.substr(pathStart);
}

}  // namespace

ComposedAgent::~ComposedAgent() { reset(); }

void ComposedAgent::reset() {
  if (host != nullptr) host->closeAgent();
  for (size_t i = registrations.size(); i > 0; --i) {
    Disposer& disposer = registrations[i - 1];
    if (disposer == nullptr) continue;
    try {
      disposer();
    } catch (const std::exception& e) {
      LOGFLF(LogLevel::warn, "[compose] 撤销注册失败: ", e.what());
    } catch (...) {
      LOGFLF(LogLevel::warn, "[compose] 撤销注册抛出未知异常");
    }
  }
  registrations.clear();
  host.reset();
  llm.reset();
  attachments.reset();
  pool.reset();
}

bool composeDiagnosticAgent(ComposedAgent& out, const std::string& agentJson,
                           const std::string& dataRoot, std::string& error) {
  out.reset();
  error.clear();

  try {
    out.deployment = loadDeployment(agentJson, dataRoot);
  } catch (const std::exception& e) {
    error = std::string("配置装载失败: ") + e.what();
    return false;
  }

  try {
    // 免费模型池: openrouter / opencode zen + model=="auto" 时有意义 (DeploymentLoader
    // 据此打开 enableModelRoute)。没有池时模型路由策略只保留重试能力。
    if (out.deployment.agent.enableModelRoute) {
      const std::string& url = out.deployment.llm.apiUrl;
      ModelPoolSource source = ModelPoolSource::OpenRouter;
      bool pooled = false;
      if (url.find("openrouter.ai") != std::string::npos) {
        source = ModelPoolSource::OpenRouter;
        pooled = true;
      } else if (url.find("opencode.ai") != std::string::npos) {
        source = ModelPoolSource::OpenCodeZen;
        pooled = true;
      }
      if (pooled) {
        std::string baseUrl;
        std::string pathPrefix;
        splitBaseUrl(url, baseUrl, pathPrefix);
        out.pool = std::make_unique<FreeModelPool>(baseUrl, pathPrefix,
                                                   out.deployment.llm.apiKey, source);
      }
    }

    // 同厂商免费聊天模型池: 固定模型 (如 glm-4.7-flash) 被 429 限流时自动同厂商轮换。
    // 前提是当前厂商在 providers.json 里至少配了 2 个免费聊天模型 (且配置的模型本身免费,
    // 付费模型不进池, 保持单模型直连)。候选顺序保证配置的模型优先, 正常请求先用它。
    if (out.pool == nullptr && !out.deployment.agent.provider.empty()) {
      std::string providerJson = agentJson;
      const auto providersRawForPool = AssetLoader::loadToMemory("config/providers.json");
      if (!providersRawForPool.empty()) {
        providerJson.assign(providersRawForPool.begin(), providersRawForPool.end());
      }
      ProviderCatalog catalog;
      catalog.load(parserJson(providerJson.c_str()));

      const std::string& vendor = out.deployment.agent.provider;
      const std::string& configured = out.deployment.llm.model;
      std::vector<std::string> chatIds;
      bool configuredFree = false;
      for (const ProviderEntry& entry : catalog.entries()) {
        if (entry.provider == vendor && entry.free && entry.chat) {
          if (entry.model == configured) configuredFree = true;
        }
      }
      if (configuredFree) {
        for (const ProviderEntry& entry : catalog.entries()) {
          if (entry.provider == vendor && entry.free && entry.chat) {
            if (entry.model == configured) {
              chatIds.insert(chatIds.begin(), entry.model);
            } else {
              chatIds.push_back(entry.model);
            }
          }
        }
      }
      if (chatIds.size() >= 2) {
        out.pool = std::make_unique<CatalogChatPool>(std::move(chatIds));
        out.deployment.agent.enableModelRoute = true;
      }
    }

    // 附件仓与日志同一 home: sessionRoot 的父目录下 sessions/ 与 attachments/ 并列
    // (dsh 的 DSH_HOME 布局)。sessionRoot 缺省时退回标准 DSH_HOME 解析。
    const std::filesystem::path sessionRootPath(out.deployment.agent.sessionRoot);
    const std::string attachmentRoot =
        sessionRootPath.empty()
            ? std::string()
            : (sessionRootPath.parent_path() / "attachments" / "v1").string();
    out.attachments = std::make_unique<DshAttachmentStore>(
        attachmentRoot, out.deployment.imageLimits);
    out.deployment.llm.attachmentStore = out.attachments.get();
    out.llm = std::make_unique<LlmProviderAdapter>(out.deployment.llm);

    out.host = std::make_unique<AgentHost>(out.deployment.agent);
    // 顺序依赖 1: 策略安装需要 llm (压缩要发摘要请求)。
    out.host->setLlmProvider(out.llm.get());
    if (out.pool != nullptr) out.host->setModelPool(out.pool.get());

    // 顺序依赖 1b: skill 发现必须在任何 builtinSkillRegistry() 访问之前注入 customSkillDirs
    // (首次访问即冻结, 之后再调 prepareBuiltinSkillRegistry() 是 no-op)。
    // 下面 registerBuiltinTools / builtinSkillRegistry().systemPrompt() 会触发首次访问, 现在注入即可。
    prepareBuiltinSkillRegistry(out.deployment.agent.customSkillDirs);

    // 会话主模型是否支持图片输入 (ProviderCatalog 的 imageInput): 决定 read_image
    // 是否注册。纯文本主模型 (DeepSeek/glm-4-flash/glm-4.7-flash) 不认 image_url,
    // read_image 会把 ImageBlock 喂给文本端点导致 HTTP 400 —— 这种情况只保留
    // see-image (vision-toolkit), 由独立视觉供应商替主模型看图, 返回文字。
    bool conversationHasImageInput = false;
    if (!out.deployment.agent.provider.empty()) {
      std::string providerJson = agentJson;
      const auto providersRawImg = AssetLoader::loadToMemory("config/providers.json");
      if (!providersRawImg.empty()) {
        providerJson.assign(providersRawImg.begin(), providersRawImg.end());
      }
      ProviderCatalog imageCatalog;
      imageCatalog.load(parserJson(providerJson.c_str()));
      const std::string& conversationVendor = out.deployment.agent.provider;
      const std::string& conversationModel = out.deployment.llm.model;
      for (const ProviderEntry& entry : imageCatalog.entries()) {
        if (entry.provider == conversationVendor &&
            entry.model == conversationModel && entry.imageInput) {
          conversationHasImageInput = true;
        }
      }
    }

    // skill 能力目录进 system prompt 前缀 (它很少变)。skill 目录只列模型可调项,
    // 加载走单一 skill 工具, 所以这里只拼目录, 不注册任何每轮动态的提示。
    out.registrations.push_back(out.host->addPromptSection(
        "avox:skills", PROMPT_ORDER_TOOL_GUIDANCE,
        builtinSkillRegistry().systemPrompt()));

    // 视觉工具路由提示: 仅多模态主模型下注册。主模型能自己看图, read_image 是默认;
    // see-image 系列留给 OCR/对比/locate/像素差/抠图/生图等 read_image 不擅长的任务。
    // 纯文本主模型下不注册本段 (此时 read_image 不可用, see-image 的 description 已自描述)。
    if (conversationHasImageInput) {
      const std::string visionRouting =
          "## 视觉任务路由\n"
          "- 看图提问/分析 (含 OCR 抄录): 直接 read_image。主模型本身支持图片输入, "
          "图片进上下文后既能看图也能 OCR, 还能结合上下文理解 (菜单单/合同条款/代码截图), "
          "一次到位; see-image 的 mode=ocr / ocr-screenshot 是为纯文本主模型准备的, 多模态主模型不绕。\n"
          "- 在图中定位元素并取像素框: locate。框可喂给 crop 裁剪或 see-image 的 region 参数。\n"
          "- 两张图对比: see-image 的 compareWith, 或 pixel-diff 给热力图 + 整体差异百分比。\n"
          "- 抠前景 / 取主色 / 局部裁剪放大: extract-foreground / dominant-colors / crop。\n"
          "- 文生图: generate-image。";
      out.registrations.push_back(out.host->addPromptSection(
          "avox:vision-routing", PROMPT_ORDER_TOOL_GUIDANCE, visionRouting));
    }

    // 顺序依赖 2: skill 注册 (builtinSkillRegistry 首次访问即装载) 必须在工具之前 ——
    // 工具集 (含 skill 工具) 需要 skill 集合已就绪。附件仓此前已建好 (llm 适配器之前),
    // 交给 read_image 换内容寻址引用。
    std::vector<Disposer> toolDisposers =
        registerBuiltinTools(*out.host, out.attachments.get(),
                             conversationHasImageInput);
    for (Disposer& disposer : toolDisposers) {
      out.registrations.push_back(std::move(disposer));
    }

    // vision-toolkit 插件按 ProviderCatalog 装配: 供应商目录独立成 config/providers.json
    // (能力/url/key/tokenMeter), 视觉开关与能力路由都由它决定, 不再经过 AgentConfig。
    // providers.json 缺失时回退 agent.json 顶层模型节点, 保持旧配置可加载。
    // 装配期顺带提示占位密钥。
    {
      ProviderCatalog visionCatalog;
      std::string providerJson = agentJson;
      const auto providersRaw = AssetLoader::loadToMemory("config/providers.json");
      if (!providersRaw.empty()) {
        providerJson.assign(providersRaw.begin(), providersRaw.end());
      }
      visionCatalog.load(parserJson(providerJson.c_str()));
      if (visionCatalog.hasPendingKeys()) {
        for (const std::string& hint : visionCatalog.apiKeyHints()) {
          LOGFLF(LogLevel::warn, "[vision] ", hint.c_str());
        }
      }
      if (visionCatalog.enabled()) {
        for (Disposer& disposer :
             registerVisionTools(*out.host, visionCatalog, conversationHasImageInput)) {
          out.registrations.push_back(std::move(disposer));
        }
      }
    }

    out.host->installConfiguredPolicies();
  } catch (const std::exception& e) {
    error = std::string("装配失败: ") + e.what();
    out.reset();
    return false;
  }
  return true;
}

}
