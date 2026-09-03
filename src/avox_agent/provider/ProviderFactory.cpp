#include "ProviderFactory.hpp"

#include "ChatProvider.hpp"

#include "avox/module/LogHelper.hpp"

namespace avox {

std::unique_ptr<IAgentProvider> makeProvider(const std::string& providerName) {
  // v1: 所有配置走 ChatProvider (OpenAI 兼容); 原生 anthropic / openai-responses
  // 暂未实装, fallback + warn (绝不抛错, 避免破坏现有 agent.json)。
  if (providerName == "anthropic") {
    LOGFLF(LogLevel::warn, "provider 'anthropic' native 未实装, fallback 到 openai-chat");
  } else if (providerName == "openai-responses") {
    LOGFLF(LogLevel::warn, "provider 'openai-responses' 未实装, fallback 到 openai-chat");
  }
  return std::make_unique<ChatProvider>();
}

}
