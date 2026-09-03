// ConsoleWin32.cpp — 见 ConsoleWin32.hpp 背景说明。实现: GetProcAddress 动态解析 + 一次性缓存。
#include "avox_cmd/ConsoleWin32.hpp"

#ifdef _WIN32
namespace avox {

namespace {
// 系统 DLL 已由加载器映射进进程, GetModuleHandleW 只查表不增引用, 线程安全且极廉价。
HMODULE kernel32() {
  static HMODULE m = GetModuleHandleW(L"kernel32.dll");
  return m;
}
HMODULE user32() {
  static HMODULE m = GetModuleHandleW(L"user32.dll");
  return m;
}
}  // namespace

bool conSetUtf8Codepage() {
  using SetCpFn = BOOL(WINAPI*)(UINT);
  static SetCpFn pSetOutputCP =
      reinterpret_cast<SetCpFn>(GetProcAddress(kernel32(), "SetConsoleOutputCP"));
  static SetCpFn pSetCP =
      reinterpret_cast<SetCpFn>(GetProcAddress(kernel32(), "SetConsoleCP"));
  bool ok = false;
  if (pSetOutputCP) ok = pSetOutputCP(CP_UTF8) != FALSE;
  if (pSetCP) pSetCP(CP_UTF8);
  return ok;
}

bool conGetScreenBufferInfo(HANDLE h, CONSOLE_SCREEN_BUFFER_INFO* out) {
  using GetInfoFn = BOOL(WINAPI*)(HANDLE, CONSOLE_SCREEN_BUFFER_INFO*);
  static GetInfoFn p =
      reinterpret_cast<GetInfoFn>(GetProcAddress(kernel32(), "GetConsoleScreenBufferInfo"));
  if (!p) {
    ZeroMemory(out, sizeof(*out));
    return false;
  }
  return p(h, out) != FALSE;
}

bool conPeekConsoleInputW(HANDLE h, INPUT_RECORD* rec, DWORD n, DWORD* got) {
  using PeekFn = BOOL(WINAPI*)(HANDLE, INPUT_RECORD*, DWORD, DWORD*);
  static PeekFn p = reinterpret_cast<PeekFn>(GetProcAddress(kernel32(), "PeekConsoleInputW"));
  if (!p) {
    *got = 0;
    return false;
  }
  return p(h, rec, n, got) != FALSE;
}

HWND conGetConsoleWindow() {
  using GetWndFn = HWND(WINAPI*)();
  // GetConsoleWindow 由 kernel32.dll 导出; 极个别老环境挂 user32, 兜底再查一次。
  static GetWndFn p = reinterpret_cast<GetWndFn>(GetProcAddress(kernel32(), "GetConsoleWindow"));
  if (!p) p = reinterpret_cast<GetWndFn>(GetProcAddress(user32(), "GetConsoleWindow"));
  return p ? p() : nullptr;
}

}
#endif  // _WIN32
