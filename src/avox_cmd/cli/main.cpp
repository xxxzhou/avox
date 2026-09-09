#include "avox_cmd/CmdExecute.h"

// avox_cli — avox 命令行工具 (独立可执行, 仅一行壳)。
// 命令逻辑折进 avox.dll (avox_cmd 经 add_sub_path), 经 C 导出入口 cmdExecute
// 调用。
int main(int argc, char** argv) {
  int exitCode = avox::cmdExecute(static_cast<int32_t>(argc), argv);
  // 终端进程退出收尾: 显式 clean + TerminateProcess 跳过 DETACH 链, 根因见
  // CmdExecute.h cmdCliFinalize 注释 (ZL/wepoll 静态析构死等已死线程 → 退出僵尸)
  avox::cmdCliFinalize(exitCode);
  return exitCode;
}
