#pragma once

// ============================================================================
// Agent Teams 工具面 (对齐 dsh packages/experimental/tool-agent-team)。
//
// 十个工具: spawn_teammate / send_message / followup_task / list_agents /
// wait_agent / interrupt_agent / team_task_create / team_task_list /
// team_task_get / team_task_update。
//
// 注册策略: 全局层**一次注册** (schema 与调用者无关)。dsh 是 install(agent) 按成员
// 注册 —— C++ 侧 ScopedLayers 无内部锁, 运行期向队友作用域插层会与其它队友驱动
// 线程的装配读并发 (unordered_map 重哈希竞态); 全局注册 + 处理体内解析归属,
// 可见面完全一致。代价: 队友派生的 one-shot 子代理也看得到 schema —— 它们的调用
// 在归属解析处 TEAM_MEMBER_NOT_FOUND 响亮失败。
//
// 另注册 "team:policy" 提示段 (order 60, 与 dsh 相同): 按装配对象渲染 Lead /
// 队友两种视角, 非成员渲染空串 (空段被装配剔除, 不进提示词)。
// ============================================================================

#include <vector>

#include "avox_agent/core/Scope.hpp"

namespace avox {

class AgentHost;

// 注册 team 工具与 team:policy 段; 返回撤销器 (调用方持有, 逆序执行)。
// host.config().enableTeam 由调用方把关。
std::vector<Disposer> installTeamTools(AgentHost& host);

}
