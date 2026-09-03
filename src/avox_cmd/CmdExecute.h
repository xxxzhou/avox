#pragma once

#include <cstdint>

#include "avox/AvoxDef.h"

namespace avox {

// ============== avox_cli 进程内入口 ==============
// 统一命令入口: 注册全部子命令并按 argv 派发。
// argv 约定同 C main: argv[0] 为程序名, argv[1] 为子命令名或全局选项 (-help/-version)。
// 仅 avox_cli 经此调用 (实现折进 avox.dll); const char* argv 形态不面向高级语言。
// 签名只用 int32_t + const char** (POD), 不跨 DLL 边界传 STL。
// 对外语言 (SWIG/其它) 转发见 CmdExport.h, 与本入口无关。
extern "C" {
AVOX_EXPORT int cmdExecute(int32_t argc, const char* const* argv);
}

// ============== Python / 高级语言友好入口 (SWIG 绑定) ==============
// 单行命令便利入口: cmdline 空格分隔 (如 "play -i rtsp://x -t 15"), 双引号可包裹含空白的参数。
// 内部拆 argv 调 cmdExecute, 捕获 stdout+stderr 合并返回 (内部缓存, 下次调用失效; 同
// runChain 的 "内部持有, 下次失效" 约定)。
// cmdline 空/拆分失败返回 ""。退出码经 cmdExecuteLastExitCode() 取 (cmdExecuteLine 副作用缓存)。
// 供 Python 链脚本 avox.cmdExecuteLine(...) 调 avox_cli 子命令 (诊断链 play/record 等经此)。
extern "C" {
AVOX_EXPORT const char* cmdExecuteLine(const char* cmdline);
AVOX_EXPORT int cmdExecuteLastExitCode();
}

// ============== avox_cli 退出收尾 (仅独立进程 CLI 调用!) ==============
// cmdExecute 返回后、进程退出前调用: 显式跑 AvoxManager::clean 后
// TerminateProcess 直接终止, 跳过 CRT 静态析构与各 DLL 的 DLL_PROCESS_DETACH。
// 原因: ExitProcess 会先 TerminateProcess 杀光其余线程, 再进 DETACH 链。
// ZL(wepoll) 的 poller 线程被杀时握着 wepoll reflock 引用, mk_api.dll 静态析构
// (~EventPoller → epoll_close → reflock_unref_and_destroy) 会无限等待这些
// 已死线程释放引用 (NtWaitForKeyedEvent 无超时), 进程变僵尸(余 1 线程挂数十分钟)。
// avox_cli 是终端进程, 退出前已显式 clean, 跳过 DETACH 无副作用。
// 嵌入宿主 (agent/godot/samples/avox.cmdExecuteLine) 禁止调用: 进程应正常退出。
extern "C" {
AVOX_EXPORT void cmdCliFinalize(int exitCode);
}

}
