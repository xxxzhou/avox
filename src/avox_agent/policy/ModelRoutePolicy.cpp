#include "ModelRoutePolicy.hpp"

#include <algorithm>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <stdexcept>
#include <thread>

#include "PolicySupport.hpp"
#include "avox/module/LogHelper.hpp"

namespace avox {

namespace {

// dsh localDelay: 指数档 = min(initial * 2^(N-1), max), 乘 [1-ratio, 1+ratio] 对称
// 抖动后再封顶 max。N 从 1 起 (第一次重试拿 initial 档)。
int64_t localBackoffMs(const ModelRoutePolicyConfig& config, int retryNumber) {
  static thread_local std::mt19937 rng{std::random_device{}()};
  std::uniform_real_distribution<double> uniform(0.0, 1.0);
  // 移位上限防溢出: 40 档之后的延迟早被 max 封死, 值无差别。
  const int exponent = std::min(retryNumber - 1, 40);
  const int64_t exponential = std::min<int64_t>(
      static_cast<int64_t>(config.backoffInitialMs) << exponent,
      config.backoffMaxMs);
  const double jitter = 1.0 - config.backoffJitterRatio
                      + 2.0 * config.backoffJitterRatio * uniform(rng);
  const double delayed = std::min(static_cast<double>(exponential) * jitter,
                                  static_cast<double>(config.backoffMaxMs));
  return static_cast<int64_t>(delayed);
}

// 可取消等待 (dsh cancellableDelay): 分片睡, signal 触发立即醒。false = 等待被取消。
bool sleepAbortable(const AbortSignal* signal, int64_t delayMs) {
  if (signal != nullptr && signal->aborted()) return false;
  const auto deadline = std::chrono::steady_clock::now()
                      + std::chrono::milliseconds(delayMs);
  while (std::chrono::steady_clock::now() < deadline) {
    if (signal != nullptr && signal->aborted()) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return signal == nullptr || !signal->aborted();
}

// per-agent 的尝试计数。turn/step 变了就重置 —— 预算是「一个 step 内」的。
struct RouteState {
  std::mutex mtx;
  struct Attempts {
    int turn = 0;
    int step = 0;
    int used = 0;
    // 本 step 已经试过并失败的模型 (避免在同一步里绕回去)。
    std::vector<std::string> tried;
  };
  std::map<const Agent*, Attempts> perAgent;

  // 取本 step 的计数槽, 必要时重置。
  Attempts& slot(const Agent* agent, int turn, int step) {
    Attempts& attempts = perAgent[agent];
    if (attempts.turn != turn || attempts.step != step) {
      attempts.turn = turn;
      attempts.step = step;
      attempts.used = 0;
      attempts.tried.clear();
    }
    return attempts;
  }
};

}  // namespace

Disposer installModelRoutePolicy(AgentExtensionPoints& points, ModelPool* pool,
                                ModelRoutePolicyConfig config) {
  if (config.maxAttempts < 1) {
    throw std::runtime_error("maxAttempts 必须是正整数");
  }
  if (config.autoKeyword.empty()) {
    throw std::runtime_error("autoKeyword 不能为空");
  }
  if (config.backoffInitialMs <= 0) {
    throw std::runtime_error("backoffInitialMs 必须为正");
  }
  if (config.backoffMaxMs < config.backoffInitialMs) {
    throw std::runtime_error("backoffMaxMs 不得小于 backoffInitialMs");
  }
  if (config.backoffJitterRatio < 0 || config.backoffJitterRatio > 1) {
    throw std::runtime_error("backoffJitterRatio 须在 [0, 1]");
  }

  auto state = std::make_shared<RouteState>();
  auto configShared = std::make_shared<ModelRoutePolicyConfig>(std::move(config));

  std::vector<Disposer> registrations;

  // 选模。
  registrations.push_back(points.request.on(
      [state, pool, configShared](
          RequestPayload& payload,
          const Chain<RequestPayload, LlmCallConfig>::Next& next)
          -> LlmCallConfig {
        // 先拿到机器本来会用的配置, 再决定是否改路由 —— 于是本策略与别的路由策略可以叠加。
        LlmCallConfig proposed = next();
        if (pool == nullptr) return proposed;

        // 要不要从池里换模型:
        //  1) model == auto        —— 免费池 (openrouter/opencodezen) 的常规轮换；
        //  2) 固定模型本 step 已限流 —— CatalogChatPool: glm-4.7-flash 被 429 后自动同厂商轮换。
        const bool enabledAuto = proposed.model == configShared->autoKeyword;
        bool rotateAfterFail = false;
        {
          std::lock_guard<std::mutex> lock(state->mtx);
          RouteState::Attempts& attempts =
              state->slot(payload.agent, payload.turn, payload.step);
          rotateAfterFail =
              std::find(attempts.tried.begin(), attempts.tried.end(), proposed.model) !=
              attempts.tried.end();
        }
        if (!enabledAuto && !rotateAfterFail) return proposed;

        if (pool->stale()) pool->refresh();

        std::string picked;
        {
          std::lock_guard<std::mutex> lock(state->mtx);
          RouteState::Attempts& attempts =
              state->slot(payload.agent, payload.turn, payload.step);
          // 跳过本 step 已经失败过的模型。
          for (size_t guard = 0; guard < pool->count() + 1; ++guard) {
            std::string candidate = pool->pickNext();
            if (candidate.empty()) break;
            if (std::find(attempts.tried.begin(), attempts.tried.end(), candidate)
                == attempts.tried.end()) {
              picked = std::move(candidate);
              break;
            }
          }
        }

        if (picked.empty()) {
          // 池空 (拉取失败) 用兜底; 池非空但全冷却则保持原样, 让请求带着 auto 失败并由
          // 重试臂处理 —— 那比在这里静默换一个可能同样被限流的模型更可预期。
          if (pool->count() == 0 && !configShared->fallbackModel.empty()) {
            LOGFLF(LogLevel::warn, "[model-route] 模型池不可用, 兜底使用 ",
                   configShared->fallbackModel.c_str());
            proposed.model = configShared->fallbackModel;
          }
          return proposed;
        }

        proposed.model = picked;
        return proposed;
      }));

  // 重试 (dsh dsh-llm-retry 的执行顺序: 码认领 → Retry-After 超限放弃 → 计数 →
  // 退避 → 可取消等待 → retry)。
  registrations.push_back(points.requestError.on(
      [state, pool, configShared](
          RequestErrorPayload& payload,
          const Chain<RequestErrorPayload, RequestErrorAction>::Next& next)
          -> RequestErrorAction {
        const std::vector<std::string>& retryable = configShared->retryableCodes;
        if (std::find(retryable.begin(), retryable.end(), payload.failure.code)
            == retryable.end()) {
          // 本策略不认领这个失败 (认证失败、参数错误): 委派下去。
          return next();
        }
        // provider Retry-After 超过退避上限: 等不起就不等 (dsh normal 模式同判)。
        const int64_t retryAfter = payload.failure.providerRetryAfterMs.value_or(0);
        if (retryAfter > configShared->backoffMaxMs) {
          LOGFLF(LogLevel::warn, "[model-route] provider Retry-After ",
                 std::to_string(retryAfter).c_str(),
                 "ms 超过退避上限, 不再重试");
          return next();
        }

        int64_t delayMs = 0;
        {
          std::lock_guard<std::mutex> lock(state->mtx);
          RouteState::Attempts& attempts =
              state->slot(payload.agent, payload.turn, payload.step);
          const int retryNumber = attempts.used + 1;
          if (retryNumber >= configShared->maxAttempts) {
            LOGFLF(LogLevel::warn, "[model-route] 已尝试 ",
                   std::to_string(retryNumber).c_str(),
                   " 次仍失败, 不再重试");
            return next();
          }
          attempts.used = retryNumber;
          // 第 N 次重试 (1 起) 的指数档; Retry-After 有效时采原值 (无抖动)。
          delayMs = retryAfter > 0
                        ? retryAfter
                        : localBackoffMs(*configShared, retryNumber);
        }

        if (pool != nullptr) {
          // 池冷却只挂真正指向模型的失败 (限流/上游 5xx); 网络抖动/流卡死重试原模型,
          // 冷却一个健康模型只会白丢一个候补。具体是哪个模型失败了, 由驱动填在
          // payload.provider 之外的位置; 这里以当前会话 header 折叠出的模型为准 ——
          // 那是刚发出去的那次请求实际用的模型。
          const bool modelAtFault = payload.failure.code == "RATE_LIMITED"
                                 || payload.failure.code == "UPSTREAM_ERROR";
          if (modelAtFault) {
            payload.agent->withSession([&](Session& session) {
              const EpochHeader* header = session.requestHeader();
              if (header == nullptr) return;
              pool->markRateLimited(header->config.model);
              std::lock_guard<std::mutex> lock(state->mtx);
              RouteState::Attempts& attempts =
                  state->slot(payload.agent, payload.turn, payload.step);
              attempts.tried.push_back(header->config.model);
            });
          }
        }

        LOGFLF(LogLevel::info, "[model-route] ", payload.failure.code.c_str(),
               ", 退避 ", std::to_string(delayMs).c_str(), "ms 后重试");
        // 可取消等待: 等待期间 abort 立即醒 —— 返回 nullopt, 驱动随后的
        // throwIfAborted 会把这次失败收束成 aborted 而不是重试。
        if (!sleepAbortable(payload.signal.get(), delayMs)) {
          return std::nullopt;
        }
        return RequestRetry{};
      }));

  return combineDisposers(std::move(registrations));
}

}
