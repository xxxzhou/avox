#pragma once

// ============================================================================
// 诊断 agent 的完整装配。
//
// 这是「装配」这件事的**唯一定义处**。两个消费者共用它:
//   AgentShell      交互式 shell
//   AgentExport.cpp 跨语言导出层 (createAgentHost)
//
// 之前两边各写一遍, 结果导出层漏了 setLlmProvider —— 端到端测试第一次跑就撞上了
// 「必须先 setLlmProvider」。装配步骤有顺序依赖 (skill → 工具 → 策略 → 会话), 让它在两处
// 重复就是让那份顺序知识重复。
//
// 顺序依赖的具体内容:
//   1. LlmProvider 必须在 installConfiguredPolicies 之前 —— 压缩策略要用它发摘要请求。
//   2. skill 注册必须在工具注册之前 —— execute 的 schema 与 skill 工具集取决于 skill 集合。
//   3. 工具注册必须在 openAgent 之前 —— 首个请求的 tool schema 在那时装配。
// ============================================================================

#include <memory>
#include <string>
#include <vector>

#include "AgentHost.hpp"
#include "avox_agent/adapter/DeploymentLoader.hpp"
#include "avox_agent/adapter/DshAttachmentStore.hpp"
#include "avox_agent/adapter/FreeModelPool.hpp"
#include "avox_agent/adapter/LlmProviderAdapter.hpp"
#include "avox_agent/policy/ModelRoutePolicy.hpp"

namespace avox {

// 一份装配好的 agent (尚未打开会话)。
//
// 成员声明顺序即依赖顺序; reset() 按相反顺序拆除。
struct ComposedAgent {
  AgentDeployment deployment;
  // 模型池 (openrouter/opencodezen 的 FreeModelPool, 或同厂商免费聊天模型的
  // CatalogChatPool); 没有可轮换的池时为 null。
  std::unique_ptr<ModelPool> pool;
  // 图片附件仓 (llm 持裸指针, 须先于 llm 析构 —— 声明在 llm 之前)。
  std::unique_ptr<DshAttachmentStore> attachments;
  std::unique_ptr<LlmProviderAdapter> llm;
  std::unique_ptr<AgentHost> host;
  // 提示词段与工具的撤销器 (逆序撤销)。
  std::vector<Disposer> registrations;

  ComposedAgent() = default;
  ~ComposedAgent();

  ComposedAgent(const ComposedAgent&) = delete;
  ComposedAgent& operator=(const ComposedAgent&) = delete;

  // 拆除: 先停会话 (driver join、工具跑完、日志 flush), 再撤注册, 最后放 host/llm/pool。
  //
  // 顺序反了会让一个还在跑的工具访问已析构的注册表。
  void reset();
};

// 从 agent.json 装配一份完整可用的诊断 agent (**不打开会话**)。
//
// agentJson: agent.json 的完整内容 (顶层平铺)。
// dataRoot: 会话日志与溢出文件的根目录 (通常 <avox>/logs)。
// error: 失败原因。
//
// 返回是否成功。失败时 out 已被 reset (不留半套装配)。
bool composeDiagnosticAgent(ComposedAgent& out, const std::string& agentJson,
                           const std::string& dataRoot, std::string& error);

}
