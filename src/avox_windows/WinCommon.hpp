#pragma once

#include "avox/module/LogHelper.hpp"

// #ifdef _WIN32
// #include <minwindef.h>
// win32平台,windows.h需要在vulkan_win32.h之前
#include <comdef.h>
#include <windows.h>
#include <wrl/client.h>
// #endif

namespace avox {
template <typename U> using MComPtr = Microsoft::WRL::ComPtr<U>;

// 关闭window平台的自定义的相关min/max
#ifndef NOMINMAX
#define NOMINMAX 1
#endif

#define AVOX_WIN_LOG(WinFunc, ...)                                              \
  do {                                                                         \
    HRESULT xhr = (WinFunc);                                                   \
    if (FAILED(xhr)) {                                                         \
      _com_error err(xhr);                                                     \
      LPCTSTR errMsg = err.ErrorMessage();                                     \
      LOGFLF(LogLevel::warn, __VA_ARGS__, " win error: ", errMsg);             \
    }                                                                          \
  } while (0)

// ret需要是VkResult,非返回VkResult的函数
#define AVOX_WIN_LOG_RETURN(WinFunc, result, ...)                               \
  do {                                                                         \
    HRESULT xhr = (WinFunc);                                                   \
    if (FAILED(xhr)) {                                                         \
      _com_error err(xhr);                                                     \
      LPCTSTR errMsg = err.ErrorMessage();                                     \
      LOGFLF(LogLevel::warn, __VA_ARGS__, " win error: ", errMsg);             \
      return result;                                                           \
    }                                                                          \
  } while (0)

#define AVOX_WIN_LOG_RETURN_FALSE(ret, ...)                                     \
  AVOX_WIN_LOG_RETURN(ret, false, __VA_ARGS__)

typedef std::function<void(IRenderContext *renderTexture)> onTickHandle;

typedef LRESULT(CALLBACK *WndProc)(HWND hwnd, UINT uMsg, WPARAM wParam,
                                   LPARAM lParam);

HWND createWin32Window(HINSTANCE inst, HWND hWnd, int width, int height,
                       const char *name, WndProc wndProc, void *userData);
// 检查当前窗口是否有效 (捕获循环用: 最小化/隐藏都判无效)
bool check_window_valid(HWND window);
// 枚举阶段判定 (比 check_window_valid 宽松): 允许最小化到任务栏的窗口进列表,
// 供 ops 找到并截图 (shotWindow 会先恢复再截); 仍过滤子窗/工具窗/隐藏窗。
bool isEnumerableWindow(HWND window);
// 获取句柄对应的窗口名称
std::string getWindowName(HWND hwnd);
// 获取句柄对应的窗口大小
bool getWindowSize(HWND hwnd, int &width, int &height);
// 仅窗口采集打开时用: 窗口最小化/隐藏则恢复可见并压到Z序底部(不抢焦点), 一次性
// (Graphics Capture/GDI 抓不到最小化窗; hwnd为空=桌面采集, 不处理)
void restoreWindowForCapture(HWND hwnd);
// 获取句柄所属进程的 exe 文件名 (诊断用: 区分 UWP frame host vs 应用本体)
std::string getWindowProcessName(HWND hwnd);

}