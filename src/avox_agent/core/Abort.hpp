#pragma once

// ============================================================================
// 取消令牌。
//
// 对齐 dsh 用的 Web 标准 AbortController / AbortSignal 分离: 写端 (controller) 与只读端
// (signal) 分开, 于是把 signal 交给下游不等于把取消权也交出去。
//
// socket shutdown (解除 httplib 阻塞 recv —— 取消时唯一够得到「回调不跑」路径的手段) 由
// LlmProviderAdapter::stream 订阅本 signal 完成; 本文件只管取消语义, 不碰 fd。
//
// 三条与 dsh 一致的语义:
//   1. 第一个原因胜出。后续 abort 无效 —— 取消是一次性的事实, 不是可覆盖的状态。
//   2. 取消不抛弃已启动的工作。signal 只是通知, 等待已启动的任务跑到静止是调用方的责任。
//   3. 两个取消源可以 fuse 成一个 (工具超时: 调用方取消 + 超时计时器各是一个源)。
// ============================================================================

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <vector>

#include "Scope.hpp"
#include "SessionTypes.hpp"

namespace avox {

// throwIfAborted 抛出的异常。
//
// 驱动在 turn 边界捕获它并把 turn 结束原因记为 aborted —— 所以它是控制流的一部分,
// 不是错误。
class AbortError : public std::runtime_error {
 public:
  explicit AbortError(AgentCancelCause cause)
      : std::runtime_error("操作已取消"), abortCause(std::move(cause)) {}

  const AgentCancelCause& cause() const { return abortCause; }

 private:
  AgentCancelCause abortCause;
};

// 只读取消视图。线程安全 —— 驱动线程查询、宿主线程取消。
class AbortSignal {
 public:
  AbortSignal() = default;

  AbortSignal(const AbortSignal&) = delete;
  AbortSignal& operator=(const AbortSignal&) = delete;

  bool aborted() const {
    std::lock_guard<std::mutex> lock(mtx);
    return abortedFlag;
  }

  // 取消原因的拷贝 (未取消返回 nullopt)。返回拷贝而非引用: 跨线程读引用不安全。
  std::optional<AgentCancelCause> reason() const {
    std::lock_guard<std::mutex> lock(mtx);
    return abortReason;
  }

  void throwIfAborted() const {
    std::optional<AgentCancelCause> cause;
    {
      std::lock_guard<std::mutex> lock(mtx);
      if (!abortedFlag) return;
      cause = abortReason;
    }
    throw AbortError(cause.value_or(AgentCancelCause{CancelByUser{}}));
  }

  // 注册取消回调; 已取消则**立即同步调用**一次 (对齐 AbortSignal 的 once 语义,
  // 避免「注册晚了就永远收不到」这类竞态)。
  //
  // 回调在调用 abort 的那个线程上运行, 必须短小且不得再取锁回本对象。
  Disposer onAbort(std::function<void()> callback) {
    if (callback == nullptr) return [] {};
    size_t id = 0;
    {
      std::lock_guard<std::mutex> lock(mtx);
      if (!abortedFlag) {
        id = ++counter;
        callbacks.emplace_back(id, std::move(callback));
      }
    }
    if (id == 0) {
      // 已取消: 锁外立即回调。
      callback();
      return [] {};
    }
    auto* self = this;
    auto disposed = std::make_shared<bool>(false);
    return [self, id, disposed]() {
      if (*disposed) return;
      *disposed = true;
      self->removeCallback(id);
    };
  }

 private:
  friend class AbortController;

  // 提交取消并取出要回调的列表 (回调在锁外执行)。
  std::vector<std::function<void()>> commitAbort(AgentCancelCause cause) {
    std::lock_guard<std::mutex> lock(mtx);
    // 第一个原因胜出。
    if (abortedFlag) return {};
    abortedFlag = true;
    abortReason = std::move(cause);
    std::vector<std::function<void()>> taken;
    taken.reserve(callbacks.size());
    for (auto& entry : callbacks) taken.push_back(std::move(entry.second));
    callbacks.clear();
    return taken;
  }

  void removeCallback(size_t id) {
    std::lock_guard<std::mutex> lock(mtx);
    for (size_t i = 0; i < callbacks.size(); ++i) {
      if (callbacks[i].first != id) continue;
      callbacks.erase(callbacks.begin() + static_cast<ptrdiff_t>(i));
      return;
    }
  }

  mutable std::mutex mtx;
  bool abortedFlag = false;
  std::optional<AgentCancelCause> abortReason;
  size_t counter = 0;
  std::vector<std::pair<size_t, std::function<void()>>> callbacks;
};

// 取消令牌的写端。
class AbortController {
 public:
  AbortController() : sharedSignal(std::make_shared<AbortSignal>()) {}

  // 触发取消。第一个原因胜出, 后续调用是空操作。
  void abort(AgentCancelCause cause) {
    std::vector<std::function<void()>> callbacks =
        sharedSignal->commitAbort(std::move(cause));
    // 回调在锁外执行: 它们可能取别的锁, 持本对象的锁调外部代码是死锁的常见来源。
    for (auto& callback : callbacks) {
      try {
        callback();
      } catch (...) {
        // 吞掉: 取消已经是既成事实, 一个回调的失败不能阻止其余回调, 也无处上报。
        // 唯一的合法处置是不让它逃逸。
      }
    }
  }

  const std::shared_ptr<AbortSignal>& signal() const { return sharedSignal; }

 private:
  std::shared_ptr<AbortSignal> sharedSignal;
};

// 把两个取消源合并成一个。
//
// 用途: 工具执行同时受调用方取消与超时计时器约束, 而工具体只该看到一个 signal。
// 任一源触发即合并信号触发, 原因取先到的那个。
//
// 返回的对象持有对两个源的订阅, 析构即退订 —— 于是一次调用结束后不会在长命的调用方
// signal 上堆积回调 (那是长会话里的一种慢泄漏)。
class FusedAbort {
 public:
  FusedAbort(const std::shared_ptr<AbortSignal>& first,
             const std::shared_ptr<AbortSignal>& second) {
    // 同一个源不必合并。
    if (first == second) {
      passthrough = first;
      return;
    }
    AbortController* target = &controller;
    firstSub = ScopedRegistration(first->onAbort([target, first]() {
      target->abort(first->reason().value_or(AgentCancelCause{CancelByUser{}}));
    }));
    secondSub = ScopedRegistration(second->onAbort([target, second]() {
      target->abort(second->reason().value_or(AgentCancelCause{CancelByUser{}}));
    }));
  }

  const std::shared_ptr<AbortSignal>& signal() const {
    return passthrough != nullptr ? passthrough : controller.signal();
  }

 private:
  std::shared_ptr<AbortSignal> passthrough;
  AbortController controller;
  ScopedRegistration firstSub;
  ScopedRegistration secondSub;
};

}
