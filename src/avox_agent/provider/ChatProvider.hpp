#pragma once

#include "IAgentProvider.hpp"

namespace avox {

// OpenAI Chat Completions 协议 Provider (/v1/chat/completions, 流式 tool_calls)。
// 把原 AgentClient 的 buildRequestPayload + onChunk delta 解析原样搬入, 字节级复刻
// OpenAI 兼容路径 (覆盖 DeepSeek/Ollama/LMStudio/百度千帆/OpenRouter 等)。
class ChatProvider : public IAgentProvider {
 public:
  const char* name() override;
  std::string buildPayload(const ProviderRequest& req) override;
  bool onSseLine(const std::string& dataLine, std::vector<SseEvent>& out) override;
  bool isStreamEnd(const std::string& dataLine) override;
};

}
