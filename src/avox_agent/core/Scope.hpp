#pragma once

// ============================================================================
// 作用域分层注册原语。
//
// 对齐 dsh 的 packages/core/scope (store.ts 的 ScopedLayers / NamedEntries /
// AnonymousEntries, 以及 index.ts 的 scopeChainOf)。
//
// 解决的问题: 一个进程里可能有多个 agent (主 agent + 子 agent + 多会话), 每个 agent
// 需要自己的工具、提示词片段、变量与限制, 而所有注册都走同一个注册表。
//
// 与 dsh 的差异: dsh 的 ScopeKey 是 Cordis 派生 context 上的一个不透明 tag, 父子关系由
// bindScopeParent 单独维护。C++ 这里让 Scope 自带 parent 指针 —— 注册方本来就持有自己的
// Scope 对象, 没有必要再引入一张全局的父子关系表 (那还会带来「谁能改别人的祖先」这个
// 需要额外保护的问题)。
//
// 事件派发方向与 dsh 一致: 沿作用域链**向上**流, 永不向下。父作用域能观察它下面组合出的
// 所有 agent, 子作用域看不到兄弟或父的事件。
// ============================================================================

#include <cstddef>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "avox/AvoxDef.h"

namespace avox {

// 撤销一次注册。幂等 —— 多次调用只有第一次生效。
using Disposer = std::function<void()>;

// 一个注册作用域。按对象身份 (地址) 比较, 内容无意义。
//
// 拥有者把它作为成员持有 (ReactLoopAgent 就持有一个), 生命周期即作用域寿命。
struct Scope {
  // 上级作用域; nullptr 表示直接挂在全局层之下。
  const Scope* parent = nullptr;
};

// 作用域键: 全局层用 nullptr 表示。
using ScopeKey = const Scope*;

// 从指定作用域到根的链, **最近的在前**。
//
// nullptr 返回空链 (全局层不是链上的一环, 它由 ScopedLayers 单独持有)。
inline std::vector<ScopeKey> scopeChainOf(ScopeKey scope) {
  std::vector<ScopeKey> chain;
  for (ScopeKey it = scope; it != nullptr; it = it->parent) {
    // 环检测: 一条自引用的父链会让这里死循环, 而它只可能来自手工拼错的 parent 指针。
    for (ScopeKey seen : chain) {
      if (seen == it) {
        throw std::runtime_error("作用域父链存在环");
      }
    }
    chain.push_back(it);
  }
  return chain;
}

// 持有一个 Disposer 的 RAII 包装。
//
// dsh 那边注册的生命周期由 Cordis 的 fiber 拥有; C++ 里改成「谁注册谁持有」, 用这个
// 包装器把它变成惯用法: 成员析构即撤销注册。
class ScopedRegistration {
 public:
  ScopedRegistration() = default;
  explicit ScopedRegistration(Disposer disposer) : disposer(std::move(disposer)) {}

  ~ScopedRegistration() { reset(); }

  ScopedRegistration(const ScopedRegistration&) = delete;
  ScopedRegistration& operator=(const ScopedRegistration&) = delete;

  ScopedRegistration(ScopedRegistration&& other) noexcept
      : disposer(std::move(other.disposer)) {
    other.disposer = nullptr;
  }

  ScopedRegistration& operator=(ScopedRegistration&& other) noexcept {
    if (this != &other) {
      reset();
      disposer = std::move(other.disposer);
      other.disposer = nullptr;
    }
    return *this;
  }

  // 提前撤销 (幂等)。
  void reset() {
    if (disposer == nullptr) return;
    Disposer taken = std::move(disposer);
    disposer = nullptr;
    taken();
  }

  bool active() const { return disposer != nullptr; }

 private:
  Disposer disposer;
};

// ---------------------------------------------------------------------------
// 条目表
// ---------------------------------------------------------------------------

// 具名条目表: 一个名字在一层里只能有一个主人。
//
// 用 vector 而非 map: 需要保持插入序 (装配顺序对提示词与工具 schema 的字节稳定性有意义),
// 而一层里的条目数是几十级, 线性查找不构成成本。
//
// 注意: 不要在遍历回调里撤销注册 —— 那会让正在使用的迭代器失效。dsh 那边靠「表清空时
// 换代」规避, C++ 这里选择由调用方遵守约定, 因为遍历中撤销本身就是可疑的写法。
template <class V>
class NamedEntries {
 public:
  using Entry = std::pair<std::string, V>;

  // duplicateMessage: 重名时的诊断消息工厂 (由调用方提供, 因为只有它知道该建议什么)。
  explicit NamedEntries(std::function<std::string(const std::string&)> duplicateMessage)
      : duplicateMessage(std::move(duplicateMessage)) {}

  // 插入一个具名条目, 返回撤销器。重名抛 std::runtime_error。
  Disposer insert(const std::string& name, V value) {
    if (contains(name)) throw std::runtime_error(duplicateMessage(name));
    data.emplace_back(name, std::move(value));
    // 撤销器按名字定位: 中途别的条目增删都不影响它找到自己那条。
    auto* self = this;
    auto shared = std::make_shared<bool>(false);
    return [self, name, shared]() {
      if (*shared) return;
      *shared = true;
      self->erase(name);
    };
  }

  bool contains(const std::string& name) const {
    for (const Entry& entry : data) {
      if (entry.first == name) return true;
    }
    return false;
  }

  const V* find(const std::string& name) const {
    for (const Entry& entry : data) {
      if (entry.first == name) return &entry.second;
    }
    return nullptr;
  }

  const std::vector<Entry>& entries() const { return data; }
  bool isEmpty() const { return data.empty(); }
  size_t size() const { return data.size(); }

 private:
  void erase(const std::string& name) {
    for (size_t i = 0; i < data.size(); ++i) {
      if (data[i].first != name) continue;
      data.erase(data.begin() + static_cast<ptrdiff_t>(i));
      return;
    }
  }

  std::function<std::string(const std::string&)> duplicateMessage;
  std::vector<Entry> data;
};

// 匿名条目表: 相等的值仍是彼此独立的注册, 各自持有幂等撤销器。
//
// 用途: 同一个扩展点上的多个监听器、多个工具 schema provider、多条限制 —— 语义是累加,
// 没有「同名者胜」的概念。
template <class V>
class AnonymousEntries {
 public:
  // 插入一个条目, 返回撤销器。
  Disposer insert(V value) {
    const size_t id = ++counter;
    data.emplace_back(id, std::move(value));
    auto* self = this;
    auto disposed = std::make_shared<bool>(false);
    return [self, id, disposed]() {
      if (*disposed) return;
      *disposed = true;
      self->erase(id);
    };
  }

  // 按插入序遍历。
  template <class Fn>
  void forEach(Fn&& fn) const {
    for (const auto& entry : data) fn(entry.second);
  }

  bool isEmpty() const { return data.empty(); }
  size_t size() const { return data.size(); }

 private:
  void erase(size_t id) {
    for (size_t i = 0; i < data.size(); ++i) {
      if (data[i].first != id) continue;
      data.erase(data.begin() + static_cast<ptrdiff_t>(i));
      return;
    }
  }

  size_t counter = 0;
  std::vector<std::pair<size_t, V>> data;
};

// ---------------------------------------------------------------------------
// 分层容器
// ---------------------------------------------------------------------------

// 一个全局层 + 每作用域一层。
//
// L 必须提供 explicit L(ScopeKey) 构造与 bool isEmpty() const。
template <class L>
class ScopedLayers {
 public:
  // onChange: 层发生变更后的通知 (注册与撤销都会触发)。
  explicit ScopedLayers(std::function<void()> onChange = nullptr)
      : globalLayer(nullptr), onChange(std::move(onChange)) {}

  L& global() { return globalLayer; }
  const L& global() const { return globalLayer; }

  // 指定作用域**自己**的层; 不存在返回 nullptr。
  //
  // 故意 chain-blind: 当调用方要的是「这个作用域自己的贡献」(它自己的限制、它自己的
  // 守卫) 时, 绝不能悄悄捡到祖先的。
  const L* peek(ScopeKey scope) const {
    if (scope == nullptr) return nullptr;
    const auto it = scoped.find(scope);
    return it == scoped.end() ? nullptr : it->second.get();
  }

  // 作用域链上已存在的层, **最远祖先在前、确切作用域在最后** ——
  // 于是按序叠加时最近的作用域拥有最终发言权。
  std::vector<const L*> chainLayers(ScopeKey scope) const {
    std::vector<ScopeKey> chain = scopeChainOf(scope);
    std::vector<const L*> layers;
    // scopeChainOf 是最近在前, 这里反向遍历得到最远在前。
    for (size_t i = chain.size(); i > 0; --i) {
      const L* layer = peek(chain[i - 1]);
      if (layer != nullptr) layers.push_back(layer);
    }
    return layers;
  }

  // 具名条目的有效视图: 全局层在前, 再按作用域链叠加, 同名时最近的作用域胜。
  //
  // 覆盖时**保持原位置**只更新值 (对齐 dsh 的 Map.set 语义), 于是遮蔽不会改变装配顺序。
  template <class V, class Pick>
  std::vector<std::pair<std::string, V>> merge(ScopeKey scope, Pick pick) const {
    std::vector<std::pair<std::string, V>> merged;
    for (const auto& entry : pick(globalLayer).entries()) {
      merged.emplace_back(entry.first, entry.second);
    }
    for (const L* layer : chainLayers(scope)) {
      for (const auto& entry : pick(*layer).entries()) {
        bool replaced = false;
        for (auto& existing : merged) {
          if (existing.first != entry.first) continue;
          existing.second = entry.second;
          replaced = true;
          break;
        }
        if (!replaced) merged.emplace_back(entry.first, entry.second);
      }
    }
    return merged;
  }

  // 匿名条目的有效视图: 全局层与作用域链上的贡献**全部累加**, 不遮蔽。
  template <class V, class Pick>
  std::vector<V> collect(ScopeKey scope, Pick pick) const {
    std::vector<V> collected;
    pick(globalLayer).forEach([&](const V& value) { collected.push_back(value); });
    for (const L* layer : chainLayers(scope)) {
      pick(*layer).forEach([&](const V& value) { collected.push_back(value); });
    }
    return collected;
  }

  // 把一次层变更绑定到它的撤销器上。
  //
  // action 接收目标层并返回它自己的 undo。撤销时执行 undo, 若该层随之变空则回收整层
  // (免得一个早已消失的 agent 在表里留下空壳), 然后通知。
  //
  // action 抛出时: 若本次调用刚创建了这一层且它仍是空的, 一并回收, 于是失败不留残迹。
  Disposer effect(ScopeKey scope, const std::function<Disposer(L&)>& action,
                  bool notify = true) {
    L* layer = nullptr;
    bool created = false;
    if (scope == nullptr) {
      layer = &globalLayer;
    } else {
      const auto it = scoped.find(scope);
      if (it == scoped.end()) {
        auto inserted = scoped.emplace(scope, std::make_unique<L>(scope));
        layer = inserted.first->second.get();
        created = true;
      } else {
        layer = it->second.get();
      }
    }

    Disposer undo;
    try {
      undo = action(*layer);
    } catch (...) {
      if (scope != nullptr && created && layer->isEmpty()) scoped.erase(scope);
      throw;
    }

    if (notify) fireChange();

    auto* self = this;
    auto disposed = std::make_shared<bool>(false);
    return [self, scope, layer, undo = std::move(undo), notify, disposed]() {
      if (*disposed) return;
      *disposed = true;
      if (undo != nullptr) undo();
      if (scope != nullptr && layer->isEmpty()) self->scoped.erase(scope);
      if (notify) self->fireChange();
    };
  }

  // 丢弃一个作用域的整层 (agent 销毁时的兜底)。
  void dropScope(ScopeKey scope) {
    if (scope == nullptr) return;
    if (scoped.erase(scope) > 0) fireChange();
  }

 private:
  void fireChange() {
    if (onChange != nullptr) onChange();
  }

  L globalLayer;
  // unique_ptr 持有: 层的地址要在 map 重哈希后保持稳定 —— 撤销器捕获了 L*。
  std::unordered_map<ScopeKey, std::unique_ptr<L>> scoped;
  std::function<void()> onChange;
};

}
