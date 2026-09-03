#include "TimeoutPolicy.hpp"

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>

#include "PolicySupport.hpp"

namespace avox {

namespace {

// 一次调用的看门狗。
//
// 用条件变量而不是 sleep: 工具正常结束时能立刻唤醒计时线程并 join 它 —— detach 一个
// sleep 线程会在进程退出时留下残留, 而长会话里这种残留会累积到几百个。
struct Watchdog {
  std::mutex mtx;
  std::condition_variable cv;
  bool finished = false;
  bool fired = false;
};

}  // namespace

Disposer installTimeoutPolicy(ToolRuntime& tools, TimeoutPolicyConfig config) {
  if (config.defaultTimeoutMs < 0) {
    throw std::runtime_error("defaultTimeoutMs 不能为负");
  }

  return tools.aroundExecute.on(
      [&tools, config](AroundToolPayload& payload,
                       const Chain<AroundToolPayload, ToolResult>::Next& next)
          -> ToolResult {
        ToolExecution& exec = *payload.exec;
        const ToolDefinition* definition =
            tools.find(exec.name, scopeOfExecution(exec));
        const int budget = (definition != nullptr && definition->timeoutMs > 0)
                               ? definition->timeoutMs
                               : config.defaultTimeoutMs;
        if (budget <= 0) return next();

        auto watchdog = std::make_shared<Watchdog>();
        auto timer = std::make_shared<AbortController>();

        std::thread timerThread([watchdog, timer, budget]() {
          std::unique_lock<std::mutex> lock(watchdog->mtx);
          const bool finishedInTime = watchdog->cv.wait_for(
              lock, std::chrono::milliseconds(budget),
              [&watchdog]() { return watchdog->finished; });
          if (finishedInTime) return;
          watchdog->fired = true;
          lock.unlock();
          timer->abort(AgentCancelCause{CancelByHook{"tool timeout"}});
        });

        // 上游 signal 必须恢复: 见本策略头文件的取舍 3。
        const std::shared_ptr<AbortSignal> upstream = exec.signal;
        FusedAbort fused(upstream, timer->signal());
        exec.signal = fused.signal();

        ToolResult result;
        // 用 exception_ptr 保存而不是在清理后裸 throw: 那时已经离开 catch 块, 裸 throw
        // 会 terminate。清理 (恢复 signal、join 计时线程) 必须无论成败都发生。
        std::exception_ptr pending;
        try {
          result = next();
        } catch (...) {
          pending = std::current_exception();
        }

        exec.signal = upstream;
        {
          std::lock_guard<std::mutex> lock(watchdog->mtx);
          watchdog->finished = true;
        }
        watchdog->cv.notify_all();
        // join 而非 detach: 撤销注册后不能还有线程活着引用本策略的东西。
        timerThread.join();

        if (pending) std::rethrow_exception(pending);

        bool firedByUs = false;
        {
          std::lock_guard<std::mutex> lock(watchdog->mtx);
          firedByUs = watchdog->fired;
        }
        // 只有**本策略的计时器赢了**才报超时。上游取消或别的包装器的期限不归本策略认领。
        if (firedByUs && !upstream->aborted()) {
          return toolError(ToolOutcome::Timeout,
                           "工具 \"" + exec.name + "\" 超过 "
                               + std::to_string(budget) + "ms 预算, 已中断",
                           TOOL_CODE_TIMEOUT);
        }
        return result;
      });
}

}
