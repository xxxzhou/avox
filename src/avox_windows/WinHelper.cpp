#include "WinExport.h"

#include "WinCommon.hpp"
#include "avox/module/AvoxManager.hpp"
#include "dx11/Dx11Window.hpp"
#include "dx12/Dx12Window.hpp"

#include <dbghelp.h>
#include <sysinfoapi.h>
#pragma comment(lib, "dbghelp.lib")

LONG WINAPI unhandledFilter(struct _EXCEPTION_POINTERS *lpExceptionInfo) {
  LONG ret = EXCEPTION_EXECUTE_HANDLER;
  TCHAR szFileName[64];
  SYSTEMTIME st;
  ::GetLocalTime(&st);
  wsprintf(szFileName, TEXT("AVOX_%04d%02d%02d-%02d%02d%02d-%ld-%ld.dmp"),
           st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
           GetCurrentProcessId(), GetCurrentThreadId());
  HANDLE hFile = ::CreateFile(szFileName, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, NULL);
  if (hFile != INVALID_HANDLE_VALUE) {
    MINIDUMP_EXCEPTION_INFORMATION ExInfo;
    ExInfo.ThreadId = ::GetCurrentThreadId();
    ExInfo.ExceptionPointers = lpExceptionInfo;
    ExInfo.ClientPointers = false;
    // write the dump
    BOOL bOK = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
                                 hFile, MiniDumpNormal, &ExInfo, NULL, NULL);
    ::CloseHandle(hFile);
  }
  return ret;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD dwReason, LPVOID lpReserved) {
  if (dwReason == DLL_PROCESS_ATTACH) {
    SetUnhandledExceptionFilter((LPTOP_LEVEL_EXCEPTION_FILTER)unhandledFilter);
    // 在加载时回调
    avox::AvoxManager::Get().init();
  } else if (dwReason == DLL_PROCESS_DETACH) {
    // 静态析构前停异步日志线程, 避免退出时访问冲突(详见 AvoxManager::clean)
    avox::AvoxManager::clean();
  } else if (dwReason == DLL_THREAD_ATTACH) {
  }
  return TRUE;
}

namespace avox {

HWND createWin32Window(HINSTANCE inst, HWND hWnd, int width, int height,
                       const char *name, WndProc wndProc, void *userData) {
  const char *className = "CustomVkWindowClass";
  WNDCLASSEX wcex = {0};
  wcex.cbSize = sizeof(WNDCLASSEX);
  bool classRegistered = false;
  // 是否已经注册
  if (!GetClassInfoEx(inst, className, &wcex)) {
    wcex.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wcex.lpfnWndProc = wndProc;
    wcex.hInstance = inst;
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    // wcex.hbrBackground = NULL;
    wcex.hbrBackground = (HBRUSH)GetStockObject(GRAY_BRUSH);
    wcex.lpszClassName = className;
    if (!RegisterClassEx(&wcex)) {
      DWORD err = GetLastError();
      LOGFLF(LogLevel::error, "RegisterClassEx failed Error:", err);
      return NULL;
    }
    classRegistered = true;
  }
  // 窗口样式和尺寸计算
  DWORD style = 0;
  DWORD exStyle = 0;
  RECT rect = {0, 0, width, height};
  if (hWnd) {
    // 子窗口模式
    style = WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
  } else {
    // 顶级窗口模式
    style =
        WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
    exStyle = WS_EX_APPWINDOW;                        // 强制显示在任务栏
    AdjustWindowRectEx(&rect, style, FALSE, exStyle); // 精确计算窗口尺寸
  }
  int32_t left = 0;
  int32_t top = 0;
  if(!hWnd){
    left = CW_USEDEFAULT;
    top = CW_USEDEFAULT;
  }
  // 创建窗口
  HWND window = CreateWindowEx(exStyle, className, name, style, left,
                               top,          // 初始位置
                               rect.right - rect.left, // 正确计算后的宽度
                               rect.bottom - rect.top, // 正确计算后的高度
                               hWnd,                   // 父窗口句柄
                               NULL,                   // 无菜单
                               inst, NULL);

  if (!window) {
    DWORD err = GetLastError();
    LOGFLF(LogLevel::error, "CreateWindowEx failed ");
    if (classRegistered) {
      UnregisterClass(className, inst);
    }
    return NULL;
  }
  // 用户数据绑定
  SetWindowLongPtr(window, GWLP_USERDATA, (LONG_PTR)userData);
  // 仅顶级窗口需要前置
  if (!hWnd) {
    SetForegroundWindow(window);
  }
  return window;
}

}
