#pragma once

// ============================================================================
// 内置工具的统一注册入口。
//
// 取代原 ToolRegistry (Meyers 单例 + 硬编码枚举): 新架构的注册表是 AgentHost 里的
// ToolRuntime, 而「注册哪些工具」是装配决定, 应当写在一个显式的函数里而不是藏在单例的
// 首次初始化里。
//
// 注册顺序不影响发给模型的 schema 顺序 —— ToolRuntime 按名字字典序输出, 于是 KV cache
// 前缀在任何机器上都一致。
// ============================================================================

#include <string>
#include <vector>

#include "avox_agent/compose/AgentHost.hpp"
#include "avox_agent/core/AttachmentStore.hpp"

namespace avox {

// 注册全部内置工具到 host, 返回撤销器 (逆序撤销)。
// 包含:
std::vector<Disposer> registerBuiltinTools(AgentHost& host,
                                           AttachmentStore* attachments,
                                           bool conversationImageInput);

}
