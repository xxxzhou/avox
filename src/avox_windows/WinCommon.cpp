#include "WinCommon.hpp"

#include <chrono>
#include <thread>

#include "avox/module/AvoxManager.hpp"
#include "avox/video/WindowRender.hpp"
#include "avox_windows/dx11/Dx11CSVideoRender.hpp"
#include "avox_windows/dx11/VideoProcessRender.hpp"

namespace avox {

void regDx11Render() {
  RegFunc regFunc = {"dx11 render init", []() {
                       VRenderDesc vkRenderDesc = {};
                       vkRenderDesc.name = "dx11 Render";
                       AvoxManager::Get().vRender.regInitFunc(
                           RenderType::D3D11, vkRenderDesc,
                           []() -> VideoRender* {
                             // Dx11CSVideoRender VideoProcessRender
                             return new Dx11CSVideoRender();
                           });
                     }};
  AvoxManager::Get().initFuncs.push_back(regFunc);
}

bool check_window_valid(HWND window) {
  DWORD styles, ex_styles;
  RECT rect;

  if (!IsWindowVisible(window) || IsIconic(window)) {
    return false;
  }
  ::GetClientRect(window, &rect);
  styles = (DWORD)GetWindowLongPtr(window, GWL_STYLE);
  ex_styles = (DWORD)GetWindowLongPtr(window, GWL_EXSTYLE);

  if (ex_styles & WS_EX_TOOLWINDOW) {
    return false;
  }
  if (styles & WS_CHILD) {
    return false;
  }
  if (rect.bottom == 0 || rect.right == 0) {
    return false;
  }
  return true;
}

// 枚举阶段判定 (比 check_window_valid 宽松): 允许最小化到任务栏(IsIconic)的窗口进列表,
// 供 ops 找到并截图; 仍过滤隐藏窗(SW_HIDE, 避免系统垃圾窗洪水)/子窗/工具窗/零尺寸窗。
bool isEnumerableWindow(HWND window) {
  if (!IsWindowVisible(window)) return false;  // 隐藏(SW_HIDE)不列; IsIconic 仍可见, 保留
  DWORD styles = (DWORD)GetWindowLongPtr(window, GWL_STYLE);
  DWORD ex_styles = (DWORD)GetWindowLongPtr(window, GWL_EXSTYLE);
  if (ex_styles & WS_EX_TOOLWINDOW) return false;
  if (styles & WS_CHILD) return false;
  RECT rect{};
  ::GetClientRect(window, &rect);
  if (rect.bottom == 0 || rect.right == 0) return false;
  return true;
}

bool getWindowSize(HWND hwnd, int& width, int& height) {
  if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) {
    return false;
  }
  RECT rect = {0, 0, 0, 0};
  // ::GetClientRect(hwnd, &rect);
  ::GetWindowRect(hwnd, &rect);
  if (rect.bottom == 0 || rect.right == 0) {
    return false;
  }
  width = rect.right - rect.left;
  height = rect.bottom - rect.top;
  // 对齐到偶数: 奇数宽高在下游YUV420P/编码会失败(与WinRT采集路径同理)
  if (width % 2 != 0) {
    width++;
  }
  if (height % 2 != 0) {
    height++;
  }
  return true;
}

void restoreWindowForCapture(HWND hwnd) {
  // 仅窗口采集(hwnd非空); 桌面/显示器采集(hwnd空)不处理
  if (!hwnd) {
    return;
  }
  // Graphics Capture/GDI 都抓不到最小化/隐藏窗; 恢复可见后压到Z序底部, 不弹到用户面前
  // 仅onOpen调用一次(每个open-close周期首次不可见才恢复), 采集循环不再兜底
  if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) {
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    SetWindowPos(hwnd, HWND_BOTTOM, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    // 等 DWM 合成 + 窗口尺寸生效
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
  }
}

std::string getWindowName(HWND hwnd) {
  int32_t len = ::GetWindowTextLengthA(hwnd);
  std::string title;
  std::vector<char> temp(len + 1);
  if (len > 0) {
    ::GetWindowTextA(hwnd, temp.data(), len + 1);
    title = temp.data();
  }
  return title;
}

// 窗口所属进程的 exe 文件名 (取全路径后截 basename); 诊断用, 无需读进程内存
std::string getWindowProcessName(HWND hwnd) {
  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  if (!pid) return "";
  HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!h) return "";
  wchar_t buf[MAX_PATH] = {};
  DWORD size = MAX_PATH;
  std::string name;
  if (QueryFullProcessImageNameW(h, 0, buf, &size)) {
    std::wstring wpath(buf);
    size_t pos = wpath.find_last_of(L"\\/");
    std::wstring wbase =
        (pos != std::wstring::npos) ? wpath.substr(pos + 1) : wpath;
    char nbuf[MAX_PATH] = {};
    WideCharToMultiByte(CP_UTF8, 0, wbase.c_str(), -1, nbuf, sizeof(nbuf),
                        nullptr, nullptr);
    name = nbuf;
  }
  CloseHandle(h);
  return name;
}

}