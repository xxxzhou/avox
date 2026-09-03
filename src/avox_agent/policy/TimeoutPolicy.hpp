#pragma once

// ============================================================================
// 工具调用超时。
//
// 对齐 dsh 的 packages/guard/timeout-policy。挂在 tools/execute (around-dispatch) 上。
//
// 防的具体故障: 合作式工具挂死 (网络、子进程、语言服务器)。旧实现 (已删) 只有 SSE 的
// reasoningTimeout 管模型无进展, 工具执行本身没有任何期限。
//
// 三个必须照搬的取舍:
//   1. **不抛弃已启动的工具体**。计时器只是 abort 信号, 之后仍然等工具返回, 再把结论
//      改写成超时 —— 同进程代码杀不掉, 抛弃它会留下写了一半的文件或残留子进程。
//   2. 用**自己的计时器是否触发**来判定超时, 而不是看信号是否 aborted。否则外层的用户
//      取消会被误报成本策略的超时, 而那两件事的处置完全不同。
//   3. 结束时**恢复上游 signal**, 免得后续的 post-execute 监听器看到一个已被本策略
//      abort 的信号, 把一次正常结果误判成取消。
// ============================================================================

#include "avox_agent/core/Scope.hpp"
#include "avox_agent/core/ToolRuntime.hpp"

namespace avox {

struct TimeoutPolicyConfig {
  // 未声明 timeoutMs 的工具的默认预算 (毫秒); 0 表示这类工具无期限。
  //
  // 工具自己声明的 timeoutMs 优先 —— 那是工具作者对「我最长可能跑多久」的判断。
  int defaultTimeoutMs = 0;
};

// 安装超时策略; 返回撤销器。
Disposer installTimeoutPolicy(ToolRuntime& tools, TimeoutPolicyConfig config);

}
