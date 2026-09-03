#pragma once

// ============================================================================
// agent-instructions: 自动装载工作区指令文件 (AGENTS.md / CLAUDE.md)。
//
// 对齐 dsh 的 packages/context/agent-instructions。行为分两条:
//   1. 基线注入: 会话第一个真正有输入的 step, 把「项目根 → 会话 cwd」目录链上
//      每层存在的候选指令文件合成一条上下文, 塞到该 step 的批次最前。此后模型
//      从一开始就知道项目的约束与约定。
//   2. 动态投影: 模型经 read/write/edit 触碰到某个文件时, 把「会话 cwd 与文件
//      所在目录之间」新出现的子目录指令文件注入后续 step —— 进入一个新目录就能
//      自动看到那层目录的上下文, 与 dsh 的 tools/result 投影等价。
// 工作区 cwd 只认会话头里显式给出的值 (SessionHeader::cwd, 来自 agent.json 的
// sessionCwd)。缺省为「无 cwd」: 无项目可扫, 基线不注入、动态投影不触发, 也不回退
// 到进程启动目录 —— 用哪个目录开进程与这些指令归置无关 (与 dsh 读 session.header.cwd
// 一致)。
//
// 与 dsh 的差异 (avox 是同步 C++): 没有异步投影队列, 动态投影直接在 tools/
// post-execute 链里同步完成, 经 PostToolAccept::additionalContexts 排进 next-step
// inbox, 下一 step 自然进模型历史。版本去重用「会话 → 作用域 → 内容指纹」的缓存,
// 不重复注入已经见过的指令文件; 文件内容变了会以 replace 措辞重新注入。
// ============================================================================

#include <string>
#include <vector>

#include "avox_agent/core/Agent.hpp"
#include "avox_agent/core/Scope.hpp"
#include "avox_agent/core/ToolRuntime.hpp"
#include "avox_agent/core/ToolTypes.hpp"

namespace avox {

// agent-instructions 插件的配置。字段均驼峰/无下划线 (工程约定禁用下划线命名)。
// agent.json 顶层 "agentInstructions" 节点结构:
//   "agentInstructions": {
//     "enabled": true,                       // 走 AgentConfig.enableAgentInstructions
//     "projectRootMarkers": [".git"],        // 标识项目根的目录条目 (向上查找)
//     "instructionFileCandidates": ["AGENTS.md", "CLAUDE.md"],
//     "localInstructionFileCandidates": ["AGENTS.local.md", "CLAUDE.local.md"],
//     "maxBytes": 32768,                     // 单次注入的渲染字节预算
//     "maxSourceBytes": 1048576              // 单文件读取上限 (超过则忽略)
//   }
struct AgentInstructionsConfig {
  std::vector<std::string> projectRootMarkers = {".git"};
  std::vector<std::string> instructionFileCandidates = {"AGENTS.md", "CLAUDE.md"};
  std::vector<std::string> localInstructionFileCandidates = {"AGENTS.local.md",
                                                             "CLAUDE.local.md"};
  int maxBytes = 32 * 1024;
  int maxSourceBytes = 1024 * 1024;
};

// 安装 agent-instructions: 基线注入 (agent/pre-step) + 动态投影 (tools/post-execute)。
// 返回撤销器 (两个监听器合并撤销)。
Disposer installAgentInstructions(ToolRuntime& tools, AgentExtensionPoints& points,
                                  const AgentInstructionsConfig& config);

}
