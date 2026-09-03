#include "avox_agent/AgentExport.h"

// avox_agent — agent 对话工具 (独立可执行, 仅一行壳)。
// 交互逻辑折进 avox.dll (AgentShell 经 add_sub_path), 经 C 导出入口 agentShellRun 调用。
int main() {
  return avox::agentShellRun();
}
