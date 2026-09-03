#include "AgentHost.hpp"

#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <utility>

#include "avox/module/LogHelper.hpp"
#include "avox_agent/core/DshLayout.hpp"
#include "avox_agent/plugins/agent-instructions/AgentInstructions.hpp"
#include "avox_agent/plugins/token-meter/TokenMeter.hpp"
#include "avox_agent/policy/CompactionPolicy.hpp"
#include "avox_agent/policy/PolicySupport.hpp"
#include "avox_agent/policy/RepeatToolPolicy.hpp"
#include "avox_agent/policy/SpillPolicy.hpp"
#include "avox_agent/policy/TimeoutPolicy.hpp"
#include "avox_agent/team/TeamService.hpp"

namespace avox {

namespace {

std::string timestampId() {
  const auto now = std::chrono::system_clock::now();
  const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
                           now.time_since_epoch())
                           .count();
  return "session-" + std::to_string(seconds);
}

}  // namespace

AgentHost::AgentHost(AgentConfig config)
    : hostConfig(std::move(config)), approvalService(hostConfig.approval) {
  // harness 身份与部署人格: 两段固定的 order, 让 preset 将来能用同名段遮蔽人格。
  policyDisposers.push_back(systemPrompt.section(
      PromptSection{"harness:identity", PROMPT_ORDER_IDENTITY,
                    [](const AssembleContext&) {
                      return std::string("你是 avox 多媒体诊断助手。");
                    }}));
  if (!hostConfig.persona.empty()) {
    const std::string persona = hostConfig.persona;
    policyDisposers.push_back(systemPrompt.section(
        PromptSection{PROMPT_SECTION_PERSONA, PROMPT_ORDER_PERSONA,
                      [persona](const AssembleContext&) { return persona; }}));
  }

  // 工具 schema 的唯一来源接到注册表 —— 排序与限制过滤都在那边完成。
  systemPrompt.setToolsProvider([this](const AssembleContext& context) {
    return toolRuntime.schemasJson(context.scope);
  });
}

AgentHost::~AgentHost() { shutdown(); }

void AgentHost::setApprovalAnswerer(
    std::function<ApprovalOutcome(const ApprovalRequest&)> answerer) {
  approvalService.setAnswerer(std::move(answerer));
}

Disposer AgentHost::defineTool(ToolDefinition definition) {
  return toolRuntime.define(std::move(definition));
}

Disposer AgentHost::addPromptSection(std::string name, int order,
                                     std::string text) {
  return systemPrompt.section(PromptSection{
      std::move(name), order,
      [text = std::move(text)](const AssembleContext&) { return text; }});
}

Disposer AgentHost::setRuntimeContext(std::string name, int order,
                                      std::function<std::string()> text) {
  if (text == nullptr) throw std::runtime_error("运行时上下文缺少求值函数");
  return systemPrompt.context(PromptContext{
      std::move(name), order,
      [text = std::move(text)](const AssembleContext&) { return text(); }});
}

void AgentHost::installConfiguredPolicies() {
  if (policiesInstalled) throw std::runtime_error("策略已安装过");
  if (llm == nullptr) {
    throw std::runtime_error("装配期必须先 setLlmProvider");
  }
  policiesInstalled = true;

  // 顺序即代码顺序。溢出裁剪注册成 prepend, 所以它无论写在哪里都最后收口结果。
  if (hostConfig.enableTimeout) {
    policyDisposers.push_back(
        installTimeoutPolicy(toolRuntime, hostConfig.timeout));
  }
  if (hostConfig.enableRepeatGuard) {
    policyDisposers.push_back(installRepeatToolPolicy(
        toolRuntime, extensionPoints, hostConfig.repeatGuard));
  }
  if (hostConfig.enableSpill && hostConfig.spill.maxInlineBytes > 0) {
    policyDisposers.push_back(installSpillPolicy(toolRuntime, hostConfig.spill));
  }
  if (hostConfig.enableApproval) {
    policyDisposers.push_back(approvalService.install(toolRuntime, systemPrompt));
  }
  if (hostConfig.enableCompaction) {
    policyDisposers.push_back(
        installCompactionPolicy(extensionPoints, *llm, hostConfig.compaction));
  }
  if (hostConfig.enableModelRoute) {
    policyDisposers.push_back(installModelRoutePolicy(
        extensionPoints, modelPool, hostConfig.modelRoute));
  }
  if (hostConfig.enableTokenMeter) {
    policyDisposers.push_back(
        installTokenMeter(extensionPoints, hostConfig.tokenMeter));
  }
  if (hostConfig.enableAgentInstructions) {
    policyDisposers.push_back(installAgentInstructions(
        toolRuntime, extensionPoints, hostConfig.agentInstructions));
  }
}

Agent* AgentHost::openAgent(const std::string& sessionId) {
  if (agent != nullptr) throw std::runtime_error("已有打开的会话");
  if (llm == nullptr) throw std::runtime_error("必须先 setLlmProvider");
  if (hostConfig.sessionRoot.empty()) {
    throw std::runtime_error("必须配置 sessionRoot");
  }

  // dsh 目录布局: <root>/<projectKey(cwd)>/<encodeSegment(id)>/session.jsonl。
  // cwd 是布局的一部分 —— resume 必须在同一项目目录下发起 (与 dsh 一致), 换目录同 id
  // 会开新会话而不是接旧档。
  const std::string id = sessionId.empty() ? timestampId() : sessionId;
  // 会话 cwd 仅来自显式配置 (缺省为空 -> 无 cwd, 日志落到 dsh 布局的 _no-cwd)。
  // 不再回退到进程 cwd: 目录在哪里启动进程与这些指令归置无关。
  const std::optional<std::string>& cwd = hostConfig.sessionCwd;
  currentSessionPath =
      dshSessionLogPath(hostConfig.sessionRoot, cwd, SessionId(id));
  std::filesystem::create_directories(
      std::filesystem::path(currentSessionPath).parent_path());

  std::unique_ptr<Session> session;
  if (std::filesystem::exists(currentSessionPath)) {
    // resume: 恢复完整历史 (含工具调用与结果) 与未做完的 inbox 工作。
    LoadedSession loaded = loadSession(currentSessionPath);
    // 路径与头行互证 (dsh assertStoredIdentity): 目录名由 id 编码而来, 头行必须一致;
    // cwd 不符说明日志被搬过目录, 拒读比静默混录安全。比较整份 optional: 会话是否
    // 绑定了项目目录必须与本次显式配置一致 ("有 cwd" 与 "无 cwd" 都算一个状态)。
    if (loaded.header.id != SessionId(id)) {
      throw std::runtime_error("会话日志头行 id ("
                               + loaded.header.id.value + ") 与目录名 ("
                               + id + ") 不符: " + currentSessionPath);
    }
    if (loaded.header.cwd != cwd) {
      const std::string stored = loaded.header.cwd.value_or("(未指定 cwd)");
      const std::string now = cwd.value_or("(未指定 cwd)");
      throw std::runtime_error("会话日志属于项目目录 "
                               + stored + " (当前是 " + now
                               + "), 拒绝跨项目 resume: " + currentSessionPath);
    }
    if (loaded.skippedIgnorable > 0) {
      LOGFLF(LogLevel::info, "[host] resume 跳过了 ",
             std::to_string(loaded.skippedIgnorable).c_str(), " 条可忽略事件");
    }
    session = std::make_unique<Session>(SessionId(id), std::move(loaded.events),
                                        std::move(loaded.header));
  } else if (std::filesystem::exists(currentSessionPath + ".zstd")) {
    throw std::runtime_error("会话 " + id
                             + " 只有 zstd 压缩档 (session.jsonl.zstd); avox 全明文,"
                               " 请在 dsh 侧配置 compression:'none' 后重录,"
                               " 或把该文件解压成 session.jsonl");
  } else {
    SessionHeader header;
    header.version = SESSION_FORMAT_VERSION;
    header.id = SessionId(id);
    header.createdAt = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
    header.cwd = cwd;
    session = std::make_unique<Session>(SessionId(id), std::vector<SessionEvent>{},
                                        std::move(header));
  }

  ReactLoopAgent::Deps deps;
  deps.systemPrompt = &systemPrompt;
  deps.tools = &toolRuntime;
  deps.llm = llm;
  deps.points = &extensionPoints;
  deps.maxParallelToolCalls = hostConfig.maxParallelToolCalls;

  AgentOptions options;
  options.provider = hostConfig.provider;
  options.model = hostConfig.model;
  options.maxTokens = hostConfig.maxTokens;

  agent = std::make_unique<ReactLoopAgent>(std::move(session), std::move(options),
                                           deps);

  // attach 会对齐文件与内存: resume 路径下内存里有文件中没有的事件 (补写的 interrupted、
  // 构造补的 end-seed), 必须在这里补齐, 否则下次 resume 会再补一遍。
  writer = std::make_unique<SessionWriter>();
  if (!writer->attach(agent->session(), currentSessionPath)) {
    LOGFLF(LogLevel::warn, "[host] 会话日志无法落盘: ",
           writer->lastError().c_str());
  }

  AgentLifecyclePayload payload;
  payload.agent = agent.get();
  extensionPoints.created.emit(payload, agent->scope());

  // ---- Agent Teams (enableTeam): Lead 会话隐式成队 ----
  // 服务绑定本会话: 恢复 = 重折叠 team/* 事件 + provisioning 冷裁定 + 邮箱补投。
  // 坏 Lead 日志 (折叠不变式炸) 只废掉队伍, 不拖垮主会话。
  if (hostConfig.enableTeam) {
    TeamRosterDeps teamDeps;
    teamDeps.lead = agent.get();
    teamDeps.systemPrompt = &systemPrompt;
    teamDeps.tools = &toolRuntime;
    teamDeps.llm = llm;
    teamDeps.points = &extensionPoints;
    teamDeps.sessionRoot = hostConfig.sessionRoot;
    teamDeps.sessionCwd = hostConfig.sessionCwd;
    teamDeps.maxParallelToolCalls = hostConfig.maxParallelToolCalls;
    teamDeps.leadOptions = options;
    const int leadDepth =
        agent->session().getHeader().delegationDepth.value_or(0);
    teamDeps.memberDelegationDepth =
        (leadDepth > options.subagentDepth ? leadDepth : options.subagentDepth)
        + 1;
    teamService = std::make_unique<TeamService>(hostConfig.team,
                                                std::move(teamDeps), *writer);
    try {
      teamService->recover();
    } catch (const std::exception& e) {
      LOGFLF(LogLevel::warn, "[host] 队伍恢复失败, 已禁用 team: ", e.what());
      teamService.reset();
    }
  }
  return agent.get();
}

void AgentHost::closeAgent() {
  if (agent == nullptr) return;

  // 队伍先拆: 队友停稳 (join 驱动、日志 flush) 时 Lead 与全部设施仍活着。
  if (teamService != nullptr) {
    teamService->dispose();
    teamService.reset();
  }

  // 先停驱动到静止, 再拆观察者 —— 反过来会让驱动的收尾事件写不进文件。
  agent->shutdown();

  AgentLifecyclePayload payload;
  payload.agent = agent.get();
  extensionPoints.disposed.emit(payload, agent->scope());

  if (writer != nullptr) {
    writer->detach();
    writer.reset();
  }
  agent.reset();
  currentSessionPath.clear();
}

void AgentHost::shutdown() {
  closeAgent();
  // 逆序撤销: 后装的策略先拆。
  for (size_t i = policyDisposers.size(); i > 0; --i) {
    Disposer& disposer = policyDisposers[i - 1];
    if (disposer == nullptr) continue;
    try {
      disposer();
    } catch (const std::exception& e) {
      LOGFLF(LogLevel::warn, "[host] 撤销注册失败: ", e.what());
    } catch (...) {
      LOGFLF(LogLevel::warn, "[host] 撤销注册抛出未知异常");
    }
  }
  policyDisposers.clear();
}

}
