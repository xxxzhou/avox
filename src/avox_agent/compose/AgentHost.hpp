#pragma once

// ============================================================================
// Agent 宿主: 显式装配。
//
// 取代 dsh 的插件加载器。dsh 的 cordis.yml + profile 是为了让用户不改代码换组合, 代价是
// 一整套加载器与类型图生成器; C++ 里的正确折衷是「显式装配函数 + 配置开关」——
// 装配顺序写在代码里, 一眼可读、可断点, 而参数从 agent.json 来。
//
// 持有的东西 (声明顺序即构造顺序, 也即依赖顺序):
//   ToolRuntime          工具注册表与三阶段管线
//   SystemPrompt         提示词装配
//   AgentExtensionPoints 五个 agent 级扩展点
//   ApprovalService      审批 (按配置启用)
//   策略撤销器           按配置安装的策略
//   ReactLoopAgent       当前会话的驱动 (openAgent 创建)
//   SessionWriter        JSONL 落盘
//
// 「单会话宿主」只约束 openAgent/closeAgent 管理的主会话。subagent 工具运行时会用
// 本宿主的同一批设施 (toolRuntime/systemPrompt/extensionPoints/llm, 经各 getter 借出)
// 在工具调用内创建瞬态子会话驱动 (见 compose/Subagents), 不走 openAgent —— 主会话
// 与子会话可以短暂并存, 但子会话的生灭完全包在父工具调用的栈帧里。
//
// 销毁顺序是反的, 这很重要: agent 先停 (驱动线程 join、工具跑完), 然后才拆策略与注册表 ——
// 否则一个还在跑的工具会访问已经析构的注册表。
// ============================================================================

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "AgentConfig.hpp"
#include "avox_agent/core/Agent.hpp"
#include "avox_agent/core/Llm.hpp"
#include "avox_agent/core/ReactLoopAgent.hpp"
#include "avox_agent/core/Session.hpp"
#include "avox_agent/core/SessionPersistence.hpp"
#include "avox_agent/core/SystemPrompt.hpp"
#include "avox_agent/core/ToolRuntime.hpp"
#include "avox_agent/policy/ApprovalService.hpp"
#include "avox_agent/policy/ModelRoutePolicy.hpp"

namespace avox {

class TeamService;

class AgentHost {
 public:
  explicit AgentHost(AgentConfig config);
  ~AgentHost();

  AgentHost(const AgentHost&) = delete;
  AgentHost& operator=(const AgentHost&) = delete;

  // ---- 装配期 (openAgent 之前) ----

  // LLM 提供方 (借用指针, 必须比本对象活得久)。装配期必须设置。
  void setLlmProvider(LlmProvider* provider) { llm = provider; }

  // 可轮换模型池 (借用指针); 未设置时模型路由策略只保留重试能力。
  void setModelPool(ModelPool* pool) { modelPool = pool; }

  // 审批应答方 (通常是 shell 的交互确认)。
  //
  // 不设 = fail closed: 所有列入 requireApproval 的工具全被拒。C 导出与无人值守路径
  // 就该保持不设。
  void setApprovalAnswerer(std::function<ApprovalOutcome(const ApprovalRequest&)> answerer);

  // 注册一个工具 (全局层)。
  Disposer defineTool(ToolDefinition definition);

  // 注册一段静态 system prompt 片段。
  //
  // 会话开始后修改会使 KV cache 从变动处失效 —— 需要运行期告诉模型什么, 用
  // agent->inject() 或 setRuntimeContext。
  Disposer addPromptSection(std::string name, int order, std::string text);

  // 注册一段动态运行时上下文 (进历史尾部, 前缀不动)。
  //
  // 与上次注入文本相同时不会重复发送 —— 那是 RuntimeContextProjection 的职责。
  Disposer setRuntimeContext(std::string name, int order,
                            std::function<std::string()> text);

  // 按配置安装策略。openAgent 之前调用一次。
  //
  // 装配顺序即代码顺序: 溢出裁剪注册成 prepend 所以总在最后生效, 其余按此处的先后。
  void installConfiguredPolicies();

  // ---- 会话 ----

  // 打开一个会话。
  //
  // sessionId 为空则新建一个 (以时间戳命名); 非空且对应 track 文件存在则 resume ——
  // resume 会恢复完整历史 (含工具调用与结果) 与未做完的 inbox 工作, 并给崩溃遗留的
  // 未闭合 turn 补写 interrupted。
  //
  // 已有打开的会话时抛 std::runtime_error (单会话宿主)。
  Agent* openAgent(const std::string& sessionId);

  // 关闭当前会话: 停驱动、等静止、flush 日志。幂等。
  void closeAgent();

  Agent* currentAgent() { return agent.get(); }

  // 当前会话的 Agent Teams 服务; enableTeam 关闭或会话未打开时为 null。
  // 工具层经此转发 spawn/send/task 操作 (服务绑定 Lead 会话, 随 open/close 生灭)。
  TeamService* team() { return teamService.get(); }

  // 当前会话的落盘观察者 (TeamService 借用; 会话关闭后为 null)。
  SessionWriter* sessionWriter() { return writer.get(); }

  // 停止一切并释放 (幂等)。析构会调用。
  //
  // 返回后保证: 无驱动线程、无在跑的工具、日志已 flush —— 此后宿主的回调对象可安全释放。
  void shutdown();

  ToolRuntime& tools() { return toolRuntime; }
  SystemPrompt& prompt() { return systemPrompt; }
  AgentExtensionPoints& points() { return extensionPoints; }
  ApprovalService& approval() { return approvalService; }
  const AgentConfig& config() const { return hostConfig; }
  const std::string& sessionPath() const { return currentSessionPath; }
  // 装配期设置的 LLM 提供方。瞬态子会话驱动 (Subagents) 借它发请求, 不经 openAgent。
  LlmProvider* llmProvider() { return llm; }

 private:
  AgentConfig hostConfig;

  ToolRuntime toolRuntime;
  SystemPrompt systemPrompt;
  AgentExtensionPoints extensionPoints;
  ApprovalService approvalService;

  LlmProvider* llm = nullptr;
  ModelPool* modelPool = nullptr;

  // 策略与装配期注册的撤销器。**逆序撤销** (见 combineDisposers)。
  std::vector<Disposer> policyDisposers;
  bool policiesInstalled = false;

  // agent 必须在 writer 之前销毁 (writer 是 session 的观察者)。
  std::unique_ptr<SessionWriter> writer;
  std::unique_ptr<ReactLoopAgent> agent;
  std::string currentSessionPath;
  // 队伍服务声明在最后: 逆序析构里最先拆 —— 队友停稳时 Lead 与全部设施仍活着
  // (队友借用 lead/toolRuntime/systemPrompt/extensionPoints/llm)。
  std::unique_ptr<TeamService> teamService;
};

}
