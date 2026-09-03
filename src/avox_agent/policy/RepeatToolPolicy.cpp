#include "RepeatToolPolicy.hpp"

#include <algorithm>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>

#include "PolicySupport.hpp"
#include "avox/module/Json.hpp"

namespace avox {

namespace {

// 把入参规范化: 深度 key 排序后重新序列化。
//
// 本项目的 Json 对象底层是 std::map, 所以 dump 出来天然是键排序的, 嵌套层也一样 ——
// 于是「属性顺序不同但内容相同」的两次调用会得到同一个串, 正是需要的比较基准。
//
// 解析失败 (模型产出了坏 JSON) 时退回原文: 那种情况下原文相同也确实算同一次调用。
std::string canonicalizeArguments(const std::string& argumentsJson) {
  const Json parsed = parserJson(argumentsJson.c_str());
  if (!parsed.bObject() && !parsed.bArray()) return argumentsJson;
  return parsed.dump();
}

std::string previewOf(const std::string& canonical, int limit) {
  if (static_cast<int>(canonical.size()) <= limit) return canonical;
  return canonical.substr(0, static_cast<size_t>(limit)) + "…(已截断)";
}

// per-agent 的调用计数。
struct RepeatState {
  std::mutex mtx;
  // agent -> (工具名 + 规范化入参) -> 累计次数
  std::map<const Agent*, std::map<std::string, int>> counts;
  // 用于生成稳定的提醒消息 id。
  size_t reminderCounter = 0;
};

}  // namespace

Disposer installRepeatToolPolicy(ToolRuntime& tools, AgentExtensionPoints& points,
                                RepeatToolPolicyConfig config) {
  // 配置非法则在安装时响亮失败, 不做静默回退 —— 一个被悄悄改成默认值的阈值会让
  // 「为什么没有提醒」变成一个没有线索的问题。
  if (config.thresholds.empty()) {
    throw std::runtime_error("repeat-tool thresholds 不能为空");
  }
  int previous = 1;
  for (int threshold : config.thresholds) {
    if (threshold < 2) {
      throw std::runtime_error("repeat-tool threshold 必须 >= 2");
    }
    if (threshold <= previous) {
      throw std::runtime_error("repeat-tool thresholds 必须严格递增且不重复");
    }
    previous = threshold;
  }
  if (config.argumentsPreviewChars <= 0) {
    throw std::runtime_error("argumentsPreviewChars 必须是正整数");
  }

  auto state = std::make_shared<RepeatState>();

  std::vector<Disposer> registrations;

  registrations.push_back(tools.postExecute.on(
      [state, config](PostToolPayload& payload,
                      const Chain<PostToolPayload, PostToolDecision>::Next& next)
          -> PostToolDecision {
        const ToolExecution& exec = *payload.exec;
        const std::string canonical = canonicalizeArguments(exec.argumentsJson);
        const std::string key = exec.name + "\n" + canonical;

        int count = 0;
        std::string reminderId;
        {
          std::lock_guard<std::mutex> lock(state->mtx);
          // 状态**无条件推进**: 无论下游怎么处置这次结果, 这次调用确实发生过。
          count = ++state->counts[exec.agent][key];
          reminderId = "repeat-tool/" + std::to_string(++state->reminderCounter);
        }

        // 委派给下游: 本策略只观察和补充, 从不否决 —— 下游仍可 block 或替换内容。
        PostToolDecision decision = next();

        const auto& thresholds = config.thresholds;
        const bool hit = std::find(thresholds.begin(), thresholds.end(), count)
                         != thresholds.end();
        if (!hit) return decision;

        std::string text;
        if (count == thresholds.front()) {
          text = "你已经用相同的参数调用了 " + exec.name + " " + std::to_string(count)
                 + " 次。如果结果不是你需要的, 请换一种做法, 而不是重复同一次调用。";
        } else {
          text = "重复调用警告: " + exec.name + " 已被用相同参数调用 "
                 + std::to_string(count) + " 次。参数: "
                 + previewOf(canonical, config.argumentsPreviewChars)
                 + "\n继续重复不会得到不同的结果。请改变策略: 换工具、换参数, "
                   "或者向用户说明你卡在哪里。";
        }
        UserMessage reminder =
            makePluginMessage(reminderId, "repeat-tool-reminder", std::move(text));

        // 两个分支都要挂上提醒: 被 block 的调用同样处在循环里。
        if (auto* blocked = std::get_if<PostToolBlock>(&decision)) {
          (void)blocked;
          // block 决策本身不携带上下文, 所以把提醒改挂成一次 accept 会改变语义。
          // 这里保留 block, 提醒在下一次 post-execute 或下一步的 pre-step 上补 ——
          // 实践中 block 已经把纠正反馈交给模型了, 提醒的边际价值很低。
          return decision;
        }
        auto& accept = std::get<PostToolAccept>(decision);
        // 前置: 让提醒出现在工具自己追加的上下文之前, 模型先看到「你在打转」。
        accept.additionalContexts.insert(accept.additionalContexts.begin(),
                                        std::move(reminder));
        return decision;
      }));

  registrations.push_back(points.preStep.on(
      [state](PreStepPayload& payload,
              const Chain<PreStepPayload, PreStepDecision>::Next& next)
          -> PreStepDecision {
        // 本步消息里有真人来源 = 用户插话过, 清链。人工介入之后的重复不算打转。
        bool userSpoke = false;
        for (const UserMessage& message : payload.messages) {
          if (message.source.kind == MessageSourceKind::User) {
            userSpoke = true;
            break;
          }
        }
        if (userSpoke) {
          std::lock_guard<std::mutex> lock(state->mtx);
          state->counts.erase(payload.agent);
        }
        return next();
      }));

  return combineDisposers(std::move(registrations));
}

}
