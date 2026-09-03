#pragma once

// ============================================================================
// 部署配置。
//
// 取代 dsh 的 cordis.yml + profile 分层: avox 不需要插件加载器, 但需要「所有可调参数都从
// 配置来」这条规则 —— 旧实现 (已删) 里 kBudgetSec、kMaxShow=4096、truncateChars=100、
// budgetChars=32*1024、keepRecentTurns=3、maxAttempts 上限 6 全是散在代码里的字面量。
//
// 配错**响亮地失败**: 自包含的字段在解析时就校验, 不做静默回退。一个被悄悄改回默认值的
// 阈值会让「为什么没生效」变成一个没有线索的问题。
// ============================================================================

#include <optional>
#include <string>
#include <vector>

#include "avox_agent/policy/ApprovalService.hpp"
#include "avox_agent/policy/CompactionPolicy.hpp"
#include "avox_agent/policy/ModelRoutePolicy.hpp"
#include "avox_agent/policy/RepeatToolPolicy.hpp"
#include "avox_agent/policy/SpillPolicy.hpp"
#include "avox_agent/policy/TimeoutPolicy.hpp"
#include "avox_agent/plugins/token-meter/TokenMeter.hpp"
#include "avox_agent/plugins/agent-instructions/AgentInstructions.hpp"
#include "avox_agent/team/TeamTypes.hpp"

namespace avox {

// 子代理委派 (subagent 工具) 的配置。
struct SubagentConfig {
  // 绝对委派深度上限 (dsh 默认 3; 0 = 禁止任何委派)。工具在上限处仍然可见 ——
  // 每次尝试启动都检查调用方的当前深度, 超限返回错误的工具结果而不是卸载工具。
  int maxDepth = 3;
};

// 与agent.json序列化与反序列化的程序类
struct AgentConfig {
  // ---- 路由 ----
  std::string provider;
  std::string model;
  std::optional<int> maxTokens;

  // ---- 存储 ----
  // 会话日志 (track 文件) 目录。一份文件三种用途: resume 种子、回放 fixture、事后审计。
  std::string sessionRoot;

  // 显式指定的会话工作目录 (可选)。
  //
  // 缺省为「无 cwd」: 会话不绑定任何项目, 日志落在 dsh 布局的 _no-cwd 归置, 且
  // agent-instructions 不注入工作区指令文件。仅在配置里显式给出时 (对齐 dsh 读取
  // session.header.cwd), 会话才绑定到该项目目录: 布局按 projectKey(cwd) 归置、可以
  // 在项目内 resume, agent-instructions 从「项目根 → cwd」链上装载指令。
  std::optional<std::string> sessionCwd;

  // ---- 提示词 ----
  // 部署人格 (进 order 0 的 persona 段)。
  std::string persona;

  // ---- 工具调度 ----
  // 同一批工具调用的最大并发。1 = 串行。
  int maxParallelToolCalls = 5;

  // ---- 策略 ----
  bool enableTimeout = true;
  TimeoutPolicyConfig timeout;

  bool enableRepeatGuard = true;
  RepeatToolPolicyConfig repeatGuard;

  bool enableSpill = true;
  SpillPolicyConfig spill;

  // 默认关: 开了它而没设 answerer 就是 fail closed (所有列入 requireApproval 的工具全被
  // 拒), 那对 C 导出与无人值守路径是正确姿态, 但对交互式 shell 会让人困惑。
  bool enableApproval = false;
  ApprovalService::Config approval;

  bool enableCompaction = true;
  CompactionPolicyConfig compaction;

  // 需要一个 ModelPool 才有意义 (见 AgentHost::setModelPool)。
  bool enableModelRoute = false;
  ModelRoutePolicyConfig modelRoute;

  // ---- token-meter 插件 ----
  // 每轮积分计量 (输入/缓存命中/输出 + 权重折算)。默认关。
  bool enableTokenMeter = false;
  TokenMeterConfig tokenMeter;

  // ---- agent-instructions 插件 ----
  // 自动装载工作区指令文件 (AGENTS.md / CLAUDE.md): 会话首步基线注入,
  // read/write/edit 触达新目录时动态投影。默认开 (对齐 dsh)。
  bool enableAgentInstructions = true;
  AgentInstructionsConfig agentInstructions;

  // ---- subagent (子代理委派) ----
  // 模型经 subagent 工具派生 one-shot 子会话: 独立日志、审批钉 never、深度受限。
  // 默认开 —— 没有它, 模型遇到可并行的子任务只能自己串行做。
  bool enableSubagent = true;
  SubagentConfig subagent;

  // ---- skill 发现 (DSH customSkillDirs 同款) ----
  // 额外 skill 目录数组 (rank 300, 在 bundled 之后加载)。空 = 仅二进制相对两层。
  // 与 binary 下的默认根合并按 DSH 优先级: project-dsh > project-agents > custom > user。
  // 顺序敏感: 数组中靠前的同名 skill 胜出 (DSH 内部亦按数组顺序裁定)。
  std::vector<std::string> customSkillDirs;

  // ---- agent-team (队友运行时) ----
  // Lead 会话隐式成队: spawn_teammate 派生 continuable 队友 (跨工具调用存活、可
  // 断点续跑), 持久邮箱 + 共享任务板以 team/* 事件落 Lead 日志。默认关。
  bool enableTeam = false;
  TeamConfig team;

  // 从 JSON 文本解析。缺失字段用默认值; 类型不符或取值非法抛 std::runtime_error。
  static AgentConfig fromJson(const std::string& json);

  // 从文件读取并解析。
  static AgentConfig fromFile(const std::string& path);
};

}
