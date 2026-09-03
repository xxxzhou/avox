#include "ApprovalService.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

#include "PolicySupport.hpp"
#include "avox/module/LogHelper.hpp"

namespace avox {

namespace {

// 模型面的策略声明。
constexpr const char* kNeverSentence =
    "本会话已禁用审批提示: 需要审批的操作会被自动拒绝。不要请求提权。";
constexpr const char* kAskSentence =
    "审批策略: ask。需要审批的操作会经由已配置的应答方询问; 没有可用应答方时按拒绝处理。";

// 策略声明放在保留历史之后 —— 与 dsh 的 order 115 同一个位置。
constexpr int kPolicyContextOrder = 115;

// 日志当前是否处在一个打开的 turn 里。
//
// 审批对必须被 turn 包住: turn 是持久日志的提交/回放边界, turn 之间的裸事件在重载时与
// 崩溃尾无法区分, 会被丢掉。
bool hasOpenTurn(const std::vector<SessionEvent>& events) {
  for (size_t i = events.size(); i > 0; --i) {
    const EventType type = events[i - 1].type;
    if (type == EventType::TurnStart) return true;
    if (type == EventType::TurnEnd) return false;
  }
  return false;
}

ApprovalPolicy foldPolicy(const std::vector<SessionEvent>& events,
                         ApprovalPolicy fallback) {
  for (size_t i = events.size(); i > 0; --i) {
    const SessionEvent& event = events[i - 1];
    if (event.type != EventType::ApprovalPolicyEvent) continue;
    return std::get<ApprovalPolicyData>(event.data).policy;
  }
  return fallback;
}

const char* policyName(ApprovalPolicy policy) {
  return policy == ApprovalPolicy::Ask ? "ask" : "never";
}

}  // namespace

ApprovalService::ApprovalService(Config configuration)
    : config(std::move(configuration)) {}

void ApprovalService::setAnswerer(
    std::function<ApprovalOutcome(const ApprovalRequest&)> newAnswerer) {
  answerer = std::move(newAnswerer);
}

ApprovalPolicy ApprovalService::effectivePolicy(Session& session) const {
  return foldPolicy(session.events(), config.defaultPolicy);
}

void ApprovalService::setPolicy(Agent& agent, ApprovalPolicy policy) {
  ApprovalPolicy previous = config.defaultPolicy;
  agent.withSession([&](Session& session) {
    previous = foldPolicy(session.events(), config.defaultPolicy);
    if (previous == policy) return;
    session.append(ApprovalPolicyData{policy});
  });
  if (previous == policy) return;

  agent.inject(makePluginMessage(
      agent.id().value + "/approval-policy/" + policyName(policy),
      "user-approval",
      std::string("审批策略已从 \"") + policyName(previous) + "\" 改为 \""
          + policyName(policy) + "\" (由用户更改)。"));
}

ApprovalOutcome ApprovalService::ask(const ToolExecution& exec,
                                    const std::string& reason) {
  Agent* agent = exec.agent;
  if (agent == nullptr) {
    // 没有 agent 就没有会话可记 —— 审批对无处安放, 只能 fail closed。
    return ApprovalOutcome::Unavailable;
  }

  // 策略在派发给应答方**之前**判定: never 意味着确定性拒绝, 不打扰任何人。
  ApprovalPolicy policy = config.defaultPolicy;
  bool turnOpen = false;
  agent->withSession([&](Session& session) {
    policy = foldPolicy(session.events(), config.defaultPolicy);
    turnOpen = hasOpenTurn(session.events());
  });

  if (!turnOpen) {
    // 不在 turn 内: 拒绝而不是硬抛。抛出会让一次工具调用变成驱动级异常, 而这本质上是
    // 调用方用错了时机 —— 按拒绝处理既安全又能留下线索。
    LOGFLF(LogLevel::warn,
           "[approval] 在 turn 之外请求审批, 已按拒绝处理 (审批对必须被 turn 包住): ",
           exec.name.c_str());
    return ApprovalOutcome::Unavailable;
  }

  const std::string requestId =
      agent->id().value + "/approval/" + std::to_string(++requestCounter);

  agent->withSession([&](Session& session) {
    ApprovalAskedData asked;
    asked.id = requestId;
    asked.toolName = exec.name;
    asked.callId = exec.callId;
    if (!reason.empty()) asked.reason = reason;
    session.append(std::move(asked));
  });

  ApprovalOutcome outcome = ApprovalOutcome::Unavailable;
  if (policy == ApprovalPolicy::Never) {
    outcome = ApprovalOutcome::Rejected;
  } else if (answerer == nullptr) {
    outcome = ApprovalOutcome::Unavailable;
  } else {
    ApprovalRequest request;
    request.agent = agent;
    request.toolName = exec.name;
    request.callId = exec.callId;
    request.reason = reason;
    try {
      // 在**锁外**等人: withSession 里做阻塞等待会挡住整个驱动。
      outcome = answerer(request);
    } catch (const std::exception& e) {
      LOGFLF(LogLevel::warn, "[approval] 应答方抛出异常, 按不可用处理: ", e.what());
      outcome = ApprovalOutcome::Unavailable;
    } catch (...) {
      outcome = ApprovalOutcome::Unavailable;
    }
  }

  agent->withSession([&](Session& session) {
    session.append(ApprovalDecidedData{requestId, outcome});
  });
  return outcome;
}

Disposer ApprovalService::install(ToolRuntime& tools, SystemPrompt& prompt) {
  tools.setApprovalAnswerer(this);

  std::vector<Disposer> registrations;

  // 哪些工具需要审批。
  registrations.push_back(tools.preExecute.on(
      [this](PreToolPayload& payload,
             const Chain<PreToolPayload, PreToolDecision>::Next& next)
          -> PreToolDecision {
        const std::string& name = payload.exec->name;
        const std::vector<std::string>& required = config.requireApproval;
        if (std::find(required.begin(), required.end(), name) == required.end()) {
          // 不需要审批: 委派下去, 让别的策略仍有发言机会。
          return next();
        }
        return PreToolAsk{"工具 \"" + name + "\" 需要用户确认"};
      }));

  // 策略声明进历史尾部而不是 system 前缀。
  registrations.push_back(prompt.context(
      PromptContext{"approval:policy", kPolicyContextOrder,
                    [this](const AssembleContext& context) -> std::string {
                      // 裸装配 (诊断、单测) 没有会话可陈述。
                      if (context.agent == nullptr) return std::string();
                      ApprovalPolicy policy = config.defaultPolicy;
                      context.agent->withSession([&](Session& session) {
                        policy = foldPolicy(session.events(), config.defaultPolicy);
                      });
                      return policy == ApprovalPolicy::Never ? kNeverSentence
                                                            : kAskSentence;
                    }}));

  auto* self = this;
  registrations.push_back([self, &tools]() { tools.setApprovalAnswerer(nullptr); });
  return combineDisposers(std::move(registrations));
}

}
