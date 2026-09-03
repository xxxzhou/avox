#pragma once

#include "avox/AvoxDef.h"

namespace avox {

// Agent 交互式 Shell: 双击 avox_agent 进入多轮对话循环。
//
// 自动加载 agent.json 默认配置 (now 指定, 否则第一个), 支持 /use 切换。
// 走新架构: AgentHost 装配 + ReactLoopAgent 驱动 + 会话日志落盘, 历史与工具轨迹完整可
// resume。交互逻辑折进 avox.dll, 经 AgentExport.h 的 agentShellRun 入口调用。
class AgentShell {
 public:
  // 运行交互循环, 返回退出码。
  static int run();
};

}
