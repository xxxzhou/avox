#pragma once

// ============================================================================
// 读 agent.json (单份平铺配置), 装出新架构要的两份配置。
//
// agent.json 顶层与 AgentConfig 字段一一对应 (provider/model/maxTokens/sessionRoot/
// persona + 各策略节点), provider 是 providers.json 里厂商名, model 是厂商 models 下
// 的模型名 —— 端点点位 (url/apiKey/apiPath) 由供应商目录提供, 不再写进 agent.json。
//
//   {
//     "provider": "zhipu",
//     "model": "glm-4.7-flash",
//     "maxTokens": 16384,
//     "persona": "...",
//     "timeout": { "enabled": true, "defaultTimeoutMs": 300000 },
//     "repeatGuard": { ... }, "spill": { ... }, "approval": { ... },
//     "compaction": { ... }, "modelRoute": { ... },
//     "tokenMeter": { ... }, "agentInstructions": { ... }, "subagent": { ... }
//   }
//
// 策略参数缺失/非法时抛 std::runtime_error —— 配错响亮地失败。
// ============================================================================

#include <string>

#include "LlmProviderAdapter.hpp"
#include "avox_agent/compose/AgentConfig.hpp"
#include "avox_agent/core/AttachmentStore.hpp"

namespace avox {

struct AgentDeployment {
  AgentConfig agent;
  LlmProviderAdapter::Config llm;
  // 图片准入限额 (agent.json 顶层 "imageLimits":{...}); 缺省 = dsh 默认值。
  // compose 据此构造 DshAttachmentStore, user 多图提交按同一组线预检。
  ImageAdmissionLimits imageLimits;
};

// 装载一份部署。
//
// agentJson: agent.json 的完整内容 (顶层平铺)。
// dataRoot: 会话日志与溢出文件的根目录 (通常是 <avox>/logs)。
//
// provider 对应 providers.json 里的厂商名, model 对应厂商 models 下的模型名。
// 缺 provider/url/字段表、策略参数非法时抛 std::runtime_error —— 配错响亮地失败。
AgentDeployment loadDeployment(const std::string& agentJson,
                              const std::string& dataRoot);

}
