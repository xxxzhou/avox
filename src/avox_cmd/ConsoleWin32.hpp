// ConsoleWin32.hpp
//
// Win32 console 调用的唯一集中点。本头里的函数一律经 GetProcAddress 动态解析, 目的: 让
// avox.dll 不再静态引用 api-ms-win-core-console-(l1-2-0|l2-1-0|l3-2-0).dll。
//
// 背景: 某些精简/定制版 Win10 镜像 (NTLite/工控定制/做 ResetBase 等) 砍掉了这几个
// api-set 转发别名, 致静态导入它们的 dll/exe 加载时报 "找不到 api-ms-win-core-console-*.dll"。
// 但这几个函数的"真身"都由 kernel32.dll 直接导出 —— kernel32 是任何能跑 Win32 exe 的系统都
// 必然存在的核心 DLL。绕过 api-set 别名、直接按名从 kernel32 解析, 兼顾"精简镜像能加载"与
// "正常机器仍可用"。
//
// 只包这 5 个 (它们映射到缺失的 l1-2-0/l2-1-0/l3-2-0)。其余 console API (GetConsoleMode/
// SetConsoleMode/ReadConsoleW/ReadConsoleInputW/GetStdHandle/GetFileType/SetConsoleCtrlHandler)
// 映射到 l1-1-0, 该 api-set 在精简镜像上仍在, 无需包装, 继续直调。

#pragma once

#include "avox/AvoxDef.h"

#ifdef _WIN32
#include <windows.h>

namespace avox {

// 设控制台输入/输出代码页为 UTF-8 (等价 SetConsoleOutputCP + SetConsoleCP(CP_UTF8))。
// 不可用安全降级, 返回 false。
bool conSetUtf8Codepage();

// 取屏幕缓冲信息 (等价 GetConsoleScreenBufferInfo)。不可用返回 false, out 清零。
bool conGetScreenBufferInfo(HANDLE h, CONSOLE_SCREEN_BUFFER_INFO* out);

// 预读输入事件不消费 (等价 PeekConsoleInputW)。不可用返回 false, *got 置 0。
bool conPeekConsoleInputW(HANDLE h, INPUT_RECORD* rec, DWORD n, DWORD* got);

// 取当前控制台窗口句柄 (等价 GetConsoleWindow)。不可用返回 nullptr。
HWND conGetConsoleWindow();

}

#endif  // _WIN32
