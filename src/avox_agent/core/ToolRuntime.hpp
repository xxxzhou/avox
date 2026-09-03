#pragma once

// ============================================================================
// 工具注册表与执行管线。
//
// 对齐 dsh 的 packages/core/tools/src/index.ts 的 ToolRuntime。
//
// 三阶段 prepare / dispatch / commit 的必要性 (四条硬理由):
//   1. 持久日志必须与模型看到的顺序一致 —— tool/call 与 tool/result 配对乱了, 续发请求
//      的消息序列在后端侧就是非法的。
//   2. 准入策略要**串行且可阻塞** (审批要等人), 所以它不能与执行重叠。
//   3. 工具体是纯 I/O、彼此不可见, 所以它**可以**重叠。
//   4. 结果必须按模型顺序提交, 于是提交阶段单独一段, 用一个只沿连续下标前进的游标。
//
// 三个扩展点各自能改什么:
//   pre-execute   allow / deny(reason) / ask(reason)。**刻意不允许改入参** ——
//                 参数已经被记录并展示给用户了, 改了就对不上。
//   execute       around-dispatch, **只能改 signal** (超时用)。调用身份不可变。
//   post-execute  accept (替换 content / 追加上下文) 或 block (把纠正反馈变成失败结果)。
//
// 另有一道单调 ToolGuard 在 pre-execute 之后、工具体之前: 只有 deny 没有 allow, 于是
// 注册顺序无法把拒绝翻回许可。
// ============================================================================

#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "Dispatch.hpp"
#include "Scope.hpp"
#include "ToolTypes.hpp"

namespace avox {

// 工具集合发生变化 (注册、注销、或某个作用域的限制变了)。
//
// 故意**不做作用域过滤**: 一次全局变更关系到每个 agent 的下次装配。
struct ToolsChangePayload {};

// prepare 的三种结局。
struct PreparedDispatch {};
// 跳过工具体, 但仍要走 post-execute 与提交 (被拒绝的调用走这条 —— 因为模型狂敲一个
// 被拒的调用正是最该被循环卫生计数的情形)。
struct PreparedPostResult {
  ToolResult result;
};
// 直接终结, 不走 post-execute (未注册的工具、派发前取消)。
struct PreparedFinalResult {
  ToolResult result;
};
using Prepared =
    std::variant<PreparedDispatch, PreparedPostResult, PreparedFinalResult>;

// 一个作用域的完整工具贡献。
class ToolLayer {
 public:
  explicit ToolLayer(ScopeKey scope);

  NamedEntries<ToolDefinition> tools;
  AnonymousEntries<ToolRestriction> restrictions;
  AnonymousEntries<ToolGuard> guards;

  bool isEmpty() const {
    return tools.isEmpty() && restrictions.isEmpty() && guards.isEmpty();
  }

  // 本层的每一条限制是否都放行这个名字 (allow 与 deny 取交集)。
  bool admits(const std::string& name) const;

  // 本层守卫给出的第一个拒绝原因。
  std::optional<std::string> guardReason(const ToolExecution& exec) const;
};

class ToolRuntime {
 public:
  ToolRuntime();

  ToolRuntime(const ToolRuntime&) = delete;
  ToolRuntime& operator=(const ToolRuntime&) = delete;

  // ---- 注册 ----

  // 注册一个工具。owner 为 nullptr 进全局层, 否则进该作用域层 (可遮蔽全局同名工具)。
  // 同一层内重名抛 std::runtime_error。
  Disposer define(ToolDefinition definition, ScopeKey owner = nullptr);

  // 限制某作用域可见的工具集合。多条限制取交集。
  Disposer restrict(ToolRestriction restriction, ScopeKey owner = nullptr);

  // 注册一道单调守卫。
  Disposer addGuard(ToolGuard guard, ScopeKey owner = nullptr);

  // 设置审批应答方 (借用指针, 生命周期须覆盖到清除或本对象销毁)。
  //
  // 为空时 ask 一律退化为 deny —— fail closed 是唯一安全的缺省。
  void setApprovalAnswerer(ApprovalAnswerer* answerer) { approval = answerer; }

  // ---- 视图 ----

  // 某作用域可见的工具, 按名字字典序。
  std::vector<const ToolDefinition*> visible(ScopeKey scope) const;

  // 按名字查找 (作用域层遮蔽全局层); 不可见返回 nullptr。
  const ToolDefinition* find(const std::string& name, ScopeKey scope) const;

  // 发给模型的 schema 数组 JSON。
  //
  // **按名字 code-unit 字典序**排列, 与 locale 无关 —— 保证任何机器上顺序一致。
  // 注册顺序不能作为依据: skill 派生的工具来自目录遍历, 而目录顺序不保证跨机器、跨文件
  // 系统一致, 顺序一变 KV cache 前缀就废了。
  //
  // 只白名单 name / description / parameters —— timeoutMs、executionMode 这些是部署事实,
  // 不是模型该知道的东西。
  std::string schemasJson(ScopeKey scope) const;

  // 一次调用能否与兄弟调用重叠 (工具未声明则 Exclusive)。
  ExecutionMode executionMode(const ToolExecution& exec) const;

  // ---- 三阶段 ----

  // 阶段一: 串行、可阻塞。跑准入策略链、审批、单调守卫。
  Prepared prepare(ToolExecution& exec);

  // 阶段二: 唯一允许重叠的阶段。跑 around 链与工具体。
  //
  // 取消**不抛弃已启动的工具体**: 等它返回, 再把成功结果改写成 Aborted。同进程代码杀不掉,
  // 抛弃它会留下写了一半的文件或残留子进程。
  ToolResult dispatch(ToolExecution& exec);

  // 阶段三: 严格按模型顺序。跑 post-execute 链, 然后通知最终结果。
  ToolResult commit(ToolExecution& exec, ToolResult result);

  // ---- 扩展点 ----

  Chain<PreToolPayload, PreToolDecision> preExecute{"tools/pre-execute"};
  Chain<AroundToolPayload, ToolResult> aroundExecute{"tools/execute"};
  Chain<PostToolPayload, PostToolDecision> postExecute{"tools/post-execute"};
  Notify<ToolResultPayload> result{"tools/result"};
  Notify<ToolsChangePayload> change{"tools/change"};

 private:
  // 把 ask 交给审批应答方; 返回归一化后的准入决策。
  PreToolDecision serviceAsk(const ToolExecution& exec, const PreToolAsk& ask);

  // 全部适用层给出的第一个守卫拒绝原因。
  std::optional<std::string> guardReason(const ToolExecution& exec) const;

  // 调用方信号是否已取消。
  static bool cancelled(const ToolExecution& exec);

  ScopedLayers<ToolLayer> layers;
  ApprovalAnswerer* approval = nullptr;
};

}
