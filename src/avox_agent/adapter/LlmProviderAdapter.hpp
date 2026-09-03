#pragma once

// ============================================================================
// 把现有的 IAgentProvider + IHttpTransport 适配成新架构的 LlmProvider。
//
// 复用的东西一个没浪费: ChatProvider 的 payload 构建与 SSE 解析 (字节级复刻 OpenAI 兼容
// 路径)、HttplibTransport 的 SSE POST、AbortSignal 直驱的 socket shutdown (内联于 stream)。
//
// 新增的只有三件事:
//   1. Message (新架构的派生历史) → ProviderMessage 的转换, 含 assistant 的 tool_calls
//      wire 生成与 tool/result 的 tool_call_id 配对, 以及 ImageBlock 经附件仓还原
//      data URL (dsh attachment 模型: 日志里只有引用, 字节在仓里)。
//   2. SseEvent → StreamChunk (dsh 的 7 变体流协议): delta 按块索引归属, 流末补
//      block-end / usage / finish 分片 —— 驱动逐分片落日志, token 级回放保真。
//   3. 失败归一化成语义码 (RATE_LIMITED / UPSTREAM_ERROR / CONNECTION_FAILED /
//      REASONING_TIMEOUT), 供 ModelRoutePolicy 判定可否重试 —— 于是 auto 轮换与退避
//      从 SSE 分帧代码里彻底搬走了。
// ============================================================================

#include <memory>
#include <string>

#include "avox_agent/core/AttachmentStore.hpp"
#include "avox_agent/core/Llm.hpp"
#include "avox_agent/provider/IAgentProvider.hpp"
#include "avox_agent/transport/IHttpTransport.hpp"

namespace avox {

class LlmProviderAdapter : public LlmProvider {
 public:
  struct Config {
    // 形如 https://openrouter.ai/api/v1
    std::string apiUrl;
    // 形如 /chat/completions
    std::string apiPath;
    std::string apiKey;
    // agent.json 的 provider 字段 (openai / anthropic / ...); 决定用哪个 IAgentProvider。
    std::string providerName;
    // 默认模型 (请求未指定时用)。
    std::string model;

    // adapter 默认值: 请求未显式指定时由本 adapter 物化, 并在 PreparedLlmCall 里标记 ——
    // 于是切换路由后新 adapter 会重新物化自己的默认值, 而用户显式设的值跨路由保留。
    float temperature = 0.7f;
    int maxTokens = 16384;
    // 推理等级: OpenAI 兼容协议用 'low' / 'medium' / 'high'。空串 = 不发, 留给非推理模型
    // 不踩雷 (OpenAI GPT-4o 拒绝 reasoning_effort; DeepSeek V3 同款)。默认 "medium" 是
    // 推理模型 (o3/o4-mini/gpt-5/r1/v3.2-exp) 的中间档, 既能开 thinking 又不烧满预算。
    // 注意: 推理等级是 per-model 能力 —— DSH settings 页刻意不放 provider 级控制, 同厂商
    // 不同模型支持档位不一致。本字段是「该 adapter 路由到的默认模型」的兜底; 用户/UI 可
    // 在选模型时显式覆盖, 然后切路由时由 adapterDefaults 标记摘除重算。
    std::string reasoningEffort = "medium";

    int connTimeoutSec = 30;
    int readTimeoutSec = 300;

    // 纯推理无进展预算 (秒); 0 = 不限。
    //
    // 保留在这一层而不是做成策略: 它是**流本身**的属性 (推理 token 不算进展), 只有正在
    // 分帧的代码知道「刚才那批 token 是正文还是推理」。
    int reasoningTimeoutSec = 0;

    // 后端不上报 usage 时按 payload 字节数估算 promptTokens 的除数。
    //
    // 压缩策略需要一个占用量判据; 没有它压缩永远不会触发。真实 usage 优先。
    int estimateBytesPerToken = 4;

    // 附件仓 (ImageBlock -> data URL 的字节来源)。非拥有指针, compose 注入; 为空时
    // 请求里出现图片块会响亮失败 —— avox 目前没有图片生产者, 这是未来图片管线的门。
    AttachmentStore* attachmentStore = nullptr;
  };

  explicit LlmProviderAdapter(Config config);
  ~LlmProviderAdapter() override;

  LlmProviderAdapter(const LlmProviderAdapter&) = delete;
  LlmProviderAdapter& operator=(const LlmProviderAdapter&) = delete;

  PreparedLlmCall prepareCall(const LlmCallConfig& proposed) override;
  LlmFinish stream(const LlmRequest& request,
                  const LlmStreamHandler& handler) override;

  // 当前配置 (供 shell 显示状态)。
  const Config& configuration() const { return config; }

 private:
  Config config;
  std::unique_ptr<IAgentProvider> provider;
  std::unique_ptr<IHttpTransport> transport;
  // scheme://host[:port]
  std::string baseUrl;
  // url 的 path 段 + apiPath
  std::string basePath;
};

}
