#pragma once

// ============================================================================
// 扩展点派发原语: Cordis 事件系统的极简替代。
//
// 对应 dsh 里 ctx.on / ctx.emit / ctx.waterfall / ctx.serial 那一层。avox 没有插件加载器,
// 所以「插件」就是注册到扩展点的函数对象, 装配顺序写在 compose/ 的显式代码里。
//
// 保留 dsh 的三种模式与它们的语义差别 —— 这个区分本身就是设计:
//
//   Notify (emit)   只观察, 不能否决。监听器异常被隔离, 不影响其余监听器与调用方。
//   Chain (waterfall) 责任链。每环拿到一个 next: 调它就委托给下一环并取回结果, 不调就
//                     短路。单决策事件里短路是正常的 (策略拥有决定权时不委托); 只做标注
//                     或观察的必须委托, 否则会静默吞掉下游。
//   Serial          按序调用, 无返回值。反对的方式是副作用 (往 inbox 里塞东西),
//                   于是「数据决定, 监听器顺序不影响结果」。
//
// 作用域过滤与 dsh 的 scopeTarget 一致: 无主监听器全收; 有主监听器只在它的作用域等于
// 派发作用域或是其祖先时才收。事件沿作用域链**向上**流, 永不向下。
// ============================================================================

#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

#include "Scope.hpp"

namespace avox {

// 记录一次被隔离的派发异常。实现在 Dispatch.cpp —— 让本头文件不必拖进日志模块。
void reportDispatchError(const char* pointName, const char* detail);

namespace dispatch_detail {

// 监听器是否应当收到这次派发。
//
// owner == nullptr 是无主监听器 (部署级), 全收。有主监听器沿派发作用域的祖先链匹配:
// 父作用域能观察它下面组合出的所有 agent, 兄弟之间互不可见。
inline bool receives(ScopeKey owner, ScopeKey dispatchScope) {
  if (owner == nullptr) return true;
  for (ScopeKey it = dispatchScope; it != nullptr; it = it->parent) {
    if (it == owner) return true;
  }
  return false;
}

}  // namespace dispatch_detail

// ---------------------------------------------------------------------------
// Notify: 只观察
// ---------------------------------------------------------------------------

template <class Payload>
class Notify {
 public:
  using Listener = std::function<void(const Payload&)>;

  explicit Notify(const char* pointName) : pointName(pointName) {}

  Disposer on(Listener listener, ScopeKey owner = nullptr) {
    return listeners.insert(Entry{std::move(listener), owner});
  }

  // 派发。监听器异常被隔离并记录 —— 观察者失败既不能否决已发生的事实, 也不能让后面的
  // 监听器收不到通知。
  void emit(const Payload& payload, ScopeKey scope = nullptr) const {
    // 先取快照: 回调中注册或注销都不影响本次遍历。
    std::vector<Listener> applicable;
    listeners.forEach([&](const Entry& entry) {
      if (dispatch_detail::receives(entry.owner, scope)) {
        applicable.push_back(entry.listener);
      }
    });
    for (const Listener& listener : applicable) {
      try {
        listener(payload);
      } catch (const std::exception& e) {
        reportDispatchError(pointName, e.what());
      } catch (...) {
        reportDispatchError(pointName, "未知异常");
      }
    }
  }

  bool isEmpty() const { return listeners.isEmpty(); }

 private:
  struct Entry {
    Listener listener;
    ScopeKey owner;
  };

  const char* pointName;
  AnonymousEntries<Entry> listeners;
};

// ---------------------------------------------------------------------------
// Chain: 责任链 (waterfall)
// ---------------------------------------------------------------------------

template <class Payload, class Result>
class Chain {
 public:
  // 委托给下一环并取回它的结果。
  //
  // 只在本次 run 期间有效 —— 不要把它保存到 handler 返回之后。
  using Next = std::function<Result()>;
  using Handler = std::function<Result(Payload&, const Next&)>;

  explicit Chain(const char* pointName) : pointName(pointName) {}

  // prepend: 必须排在普通注册之前的少数场合用。
  //
  // 典型是「先让下游定稿, 再对结果做统一处理」的策略 (结果溢出裁剪就是这种):
  // 它注册在最前, 但第一件事是调 next(), 于是它拿到的是下游已经敲定的结果。
  Disposer on(Handler handler, ScopeKey owner = nullptr, bool prepend = false) {
    Entry entry{std::move(handler), owner};
    return prepend ? prepended.insert(std::move(entry))
                   : appended.insert(std::move(entry));
  }

  // 跑一遍链。terminal 是全部委托到底之后的默认行为。
  //
  // handler 抛出的异常**不隔离**: 责任链的每一环都参与决策, 一环失败意味着这次决策不完整,
  // 让它传播给调用方去决定 (通常是把它变成一个模型可见的失败结果)。
  Result run(Payload& payload, ScopeKey scope, const Next& terminal) const {
    std::vector<Handler> applicable;
    const auto collect = [&](const Entry& entry) {
      if (dispatch_detail::receives(entry.owner, scope)) {
        applicable.push_back(entry.handler);
      }
    };
    prepended.forEach(collect);
    appended.forEach(collect);

    if (applicable.empty()) return terminal();

    // 递归构造 next: step(i) 跑第 i 环, 它的 next 是 step(i+1)。
    std::function<Result(size_t)> step;
    step = [&](size_t index) -> Result {
      if (index >= applicable.size()) return terminal();
      Next next = [&step, index]() { return step(index + 1); };
      return applicable[index](payload, next);
    };
    return step(0);
  }

  bool isEmpty() const { return prepended.isEmpty() && appended.isEmpty(); }

 private:
  struct Entry {
    Handler handler;
    ScopeKey owner;
  };

  const char* pointName;
  AnonymousEntries<Entry> prepended;
  AnonymousEntries<Entry> appended;
};

// ---------------------------------------------------------------------------
// Serial: 按序通知, 靠副作用表达意见
// ---------------------------------------------------------------------------

template <class Payload>
class Serial {
 public:
  using Listener = std::function<void(const Payload&)>;

  explicit Serial(const char* pointName) : pointName(pointName) {}

  Disposer on(Listener listener, ScopeKey owner = nullptr) {
    return listeners.insert(Entry{std::move(listener), owner});
  }

  // 按注册序逐个调用。
  //
  // 与 Notify 的区别不在实现而在契约: 这里的 payload 携带可操作的句柄 (例如 agent),
  // 监听器通过副作用表达意见, 调用方随后**重新读取数据**来决定后续行为。于是监听器
  // 顺序不影响结果 —— 这是「数据决定」的落地方式。
  //
  // 异常仍然隔离: 一个监听器的失败不该让 turn 边界卡住。
  void run(const Payload& payload, ScopeKey scope = nullptr) const {
    std::vector<Listener> applicable;
    listeners.forEach([&](const Entry& entry) {
      if (dispatch_detail::receives(entry.owner, scope)) {
        applicable.push_back(entry.listener);
      }
    });
    for (const Listener& listener : applicable) {
      try {
        listener(payload);
      } catch (const std::exception& e) {
        reportDispatchError(pointName, e.what());
      } catch (...) {
        reportDispatchError(pointName, "未知异常");
      }
    }
  }

  bool isEmpty() const { return listeners.isEmpty(); }

 private:
  struct Entry {
    Listener listener;
    ScopeKey owner;
  };

  const char* pointName;
  AnonymousEntries<Entry> listeners;
};

}
