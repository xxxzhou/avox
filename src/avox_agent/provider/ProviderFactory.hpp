#pragma once

#include <memory>
#include <string>

#include "IAgentProvider.hpp"

namespace avox {

// 按 agent.json 的 provider 字段造 Provider。v1: 全部 fallback 到 ChatProvider
// (anthropic/openai-responses 暂未实装, 仅 warn); 绝不抛错 (不破坏现有 agent.json)。
std::unique_ptr<IAgentProvider> makeProvider(const std::string& providerName);

}
