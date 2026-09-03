#pragma once

// ============================================================================
// 审批服务。
//
// 对齐 dsh 的 packages/interaction/user-approval。
//
// 补的是旧实现 (已删) 最大的安全空洞: run_code 能跑任意代码、
// 脚本型 skill 直接 spawn python —— 全部零审批, 而 toolObs 是纯事后通知 (不能否决)。
//
// 四个照搬的取舍:
//   1. 结果词汇表里**没有「永久允许」**: 一次询问的答案只能是一次性授权。持久偏好属于
//      另一个概念 (会话策略, 走 approval/policy 日志事件), 不混进同一个返回值。
//   2. **只有 allowed-once 才继续**, 其余一切 (拒绝、取消、不可用) 都变成拒绝。
//   3. 无应答方 / 应答方抛错 / 返回值不在词汇表内 → fail closed。
//   4. 审批对必须被 turn 包住: turn 是持久日志的提交/回放边界, turn 之间的裸事件在
//      重载时与崩溃尾无法区分。
//
// 策略声明走 **prompt context (历史尾部)** 而不是 section: 切换策略不该重写稳定的
// prompt 前缀。这与 dsh 把它注册成 order 115 的 context 是同一个决定。
// ============================================================================

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "avox_agent/core/Agent.hpp"
#include "avox_agent/core/Scope.hpp"
#include "avox_agent/core/SystemPrompt.hpp"
#include "avox_agent/core/ToolRuntime.hpp"

namespace avox {

// 交给宿主应答方的一次询问。
//
// 只带 callId 而不重复参数 —— 那次调用已经展示过了。
struct ApprovalRequest {
  Agent* agent = nullptr;
  std::string toolName;
  CallId callId;
  std::string reason;
};

class ApprovalService : public ApprovalAnswerer {
 public:
  struct Config {
    // 没有 approval/policy 覆盖的会话使用的默认策略。
    //
    // Ask 委派给应答方 (没有则 fail closed); Never 每次都自动拒绝、不打扰任何人 ——
    // 那是 CI / 无人值守 / C 导出路径的确定性姿态。
    ApprovalPolicy defaultPolicy = ApprovalPolicy::Ask;

    // 需要审批的工具名。
    //
    // 按名字列举而不是「危险度打分」: 谁需要审批是部署决定, 应当一眼可查、可评审。
    std::vector<std::string> requireApproval;
  };

  explicit ApprovalService(Config config);

  // 设置宿主应答方 (通常是 shell 的交互确认或 GUI 弹窗)。
  //
  // 为空 = fail closed。C 导出与无人值守路径就该保持为空。
  void setAnswerer(std::function<ApprovalOutcome(const ApprovalRequest&)> answerer);

  // 会话的有效策略: 自己的 approval/policy 折叠, 否则配置默认。
  //
  // 纯 fold 而不是独立的可变状态机 —— resume 不需要任何补追机制, 因为重放日志就是状态。
  ApprovalPolicy effectivePolicy(Session& session) const;

  // 切换一个活 agent 的策略: 落日志 + 给下一步注入一条通知。
  //
  // 注入通知是「模型可见 ⟺ 已记录」的落地: 策略变化会改变模型能做什么, 所以模型必须
  // 通过一条已记录的消息知道它。
  void setPolicy(Agent& agent, ApprovalPolicy policy);

  // ApprovalAnswerer: 由 ToolRuntime 在收到 ask 决策后调用。
  ApprovalOutcome ask(const ToolExecution& exec, const std::string& reason) override;

  // 安装: 把自己接成审批应答方, 注册「哪些工具需要审批」的准入策略, 并注册策略声明。
  Disposer install(ToolRuntime& tools, SystemPrompt& prompt);

 private:
  Config config;
  std::function<ApprovalOutcome(const ApprovalRequest&)> answerer;
  size_t requestCounter = 0;
};

}
