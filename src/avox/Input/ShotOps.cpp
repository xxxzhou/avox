#include "avox/Input/ShotOps.hpp"

#ifdef _WIN32
#include <windows.h>

#include <cctype>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "avox/AvoxLayer.h"                        // ISurfaceRender
#include "avox/AvoxPlayer.h"                       // PlayerState
#include "avox/player/SourcePlayer.hpp"           // 具体类
#include "avox_windows/winrt/CaptureWindows.hpp"  // CaptureWindowsMgr, WinCaptureBase
#endif

namespace avox {

#ifdef _WIN32
namespace {
// 大小写不敏感子串包含
bool containsCI(const std::string& hay, const char* needle) {
  if (!needle || !needle[0]) return false;
  std::string h = hay, n = needle;
  for (auto& c : h) c = (char)tolower((unsigned char)c);
  for (auto& c : n) c = (char)tolower((unsigned char)c);
  return h.find(n) != std::string::npos;
}
// 窗口所在屏幕索引 (多屏): MonitorFromWindow 取 HMONITOR, 再枚举计数得索引
int32_t windowScreenIndex(HWND hwnd) {
  if (!hwnd) return 0;
  HMONITOR hmon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
  struct Ctx {
    HMONITOR target;
    int32_t idx = 0;
    int32_t found = 0;
  };
  Ctx ctx;
  ctx.target = hmon;
  auto cb = [](HMONITOR h, HDC, LPRECT, LPARAM lp) -> BOOL {
    auto* c = reinterpret_cast<Ctx*>(lp);
    if (h == c->target) {
      c->found = c->idx;
      return FALSE;
    }
    c->idx++;
    return TRUE;
  };
  EnumDisplayMonitors(nullptr, nullptr, cb, reinterpret_cast<LPARAM>(&ctx));
  return ctx.found;
}
// 在局部 CaptureWindowsMgr 里按 wndName 子串(大小写不敏感)收集所有匹配窗口
// hwnd。 用局部 mgr (构造即枚举), 不影响全局设备表; 返回 hwnd 值副本(mgr
// 析构后仍安全)。 不能返回 WinCaptureBase*: 其生命期随局部 mgr 结束而失效。
// wndName 为空/null 时不过滤, 收集全部窗口 (供 findWindows 无筛选列举)。
std::vector<HWND> findWindowHwnds(const char* wndName) {
  std::vector<HWND> hwnds;
  CaptureWindowsMgr mgr;  // 构造即枚举 (勿再 refreshDevices, 会重复 append)
  bool matchAll = !wndName || !wndName[0];  // 空 → 全部
  int32_t count = mgr.getDeviceCount();
  for (int32_t i = 0; i < count; i++) {
    WinCaptureBase* d = mgr.getTDevice(i);
    if (!d) continue;
    const char* name = d->getDeviceName();
    if (!matchAll && !containsCI(name ? name : "", wndName)) continue;
    HWND hwnd = d->nativeHwnd();
    if (hwnd) hwnds.push_back(hwnd);
  }
  return hwnds;
}
// 前置窗口: 最小化先恢复, 再用一次 Alt 按放绕过前台锁后
// SetForegroundWindow+置顶。 shotWindow 与 activeWindow 共用; shotWindow
// 要操作已选定的确切 hwnd, 故单独提函数。
bool bringToFront(HWND hwnd) {
  if (!hwnd) return false;
  if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
  INPUT alt;
  ZeroMemory(&alt, sizeof(INPUT));
  alt.type = INPUT_KEYBOARD;
  alt.ki.wVk = VK_MENU;
  SendInput(1, &alt, sizeof(INPUT));
  alt.ki.dwFlags = KEYEVENTF_KEYUP;
  SendInput(1, &alt, sizeof(INPUT));
  bool ok = SetForegroundWindow(hwnd) != FALSE;
  BringWindowToTop(hwnd);
  return ok;
}

// 单次截图尝试 (新 SourcePlayer): open → 等首帧(state==playing) → screenShot。
// 抽自 shotWindow, 供 shotScreen 复用 — monitor/window 同走 SourcePlayer+screenShot 路径。
bool tryCapture(WinCaptureBase* src, IImageBuffer* buffer) {
  bool got = false;
  std::unique_ptr<SourcePlayer> sp = std::make_unique<SourcePlayer>();
  sp->setVideoSource(src);  // WinCaptureBase IS-A IVideoSource
  ISurfaceRender* render = sp->getSurfaceRender();
  render->setOffSurface(YuvType::other);
  if (sp->open()) {
    for (int i = 0; i < 20; i++) {
      if (sp->getState() == PlayerState::playing) {
        got = true;
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (got) {
      std::this_thread::sleep_for(std::chrono::milliseconds(80));
      got = render && render->screenShot(buffer);
    }
  }
  sp->close();
  return got;
}

// 按 dev 类型回填 WindowShotInfo 几何 (left/top/width/height) + hwnd。
// window: GetWindowRect(hwnd); monitor: GetMonitorInfo(hmon) 的 rcMonitor (解决
// shotWindow 选到 monitor 时 nativeHwnd()=null 致 left/top 丢失)。screenIndex 由调用方填。
void fillShotInfo(WinCaptureBase* dev, WindowShotInfo* info) {
  if (!dev || !info) return;
  if (dev->getDeviceKind() == VDeviceKind::monitor) {
    info->hwnd = nullptr;
    HMONITOR hmon = dev->nativeHmon();
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (hmon && GetMonitorInfoW(hmon, &mi)) {
      info->left = mi.rcMonitor.left;
      info->top = mi.rcMonitor.top;
      info->width = mi.rcMonitor.right - mi.rcMonitor.left;
      info->height = mi.rcMonitor.bottom - mi.rcMonitor.top;
    }
  } else {
    HWND hwnd = dev->nativeHwnd();
    info->hwnd = reinterpret_cast<void*>(hwnd);
    RECT rc{};
    if (hwnd && GetWindowRect(hwnd, &rc)) {
      info->left = rc.left;
      info->top = rc.top;
      info->width = rc.right - rc.left;
      info->height = rc.bottom - rc.top;
    }
  }
}

// 缩放因子: 截图 buffer 可能被渲染层归一化(缩放输出), 如 4K 窗口 → 1080p buffer。
// scale = 窗口/显示器屏幕尺寸 ÷ buffer 尺寸; toScreen 据此把 buffer 坐标放大回屏幕坐标(点击不偏)。
// 默认(无归一化) buffer 尺寸 = 窗口尺寸 → scale=1, 行为不变。
void fillShotScale(IImageBuffer* buffer, WindowShotInfo* info) {
  if (!buffer || !info) return;
  const ImageFormat fmt = buffer->getImageFormat();
  if (fmt.width > 0 && info->width > 0) {
    info->scaleX = static_cast<float>(info->width) / fmt.width;
  }
  if (fmt.height > 0 && info->height > 0) {
    info->scaleY = static_cast<float>(info->height) / fmt.height;
  }
}
}  // namespace

bool shotWindow(const char* wndName, IImageBuffer* buffer,
                WindowShotInfo* info) {
  if (!wndName || !buffer) return false;
  // 局部 mgr: 构造即枚举 (枚举含最小化窗), 不影响全局设备表
  CaptureWindowsMgr mgr;
  // 收集所有 substring 匹配的设备 (UWP 会成对: CoreWindow 内容窗 +
  // ApplicationFrameWindow 框架窗)
  std::vector<WinCaptureBase*> devs;
  int32_t count = mgr.getDeviceCount();
  for (int32_t i = 0; i < count; i++) {
    WinCaptureBase* d = mgr.getTDevice(i);
    if (!d) continue;
    const char* name = d->getDeviceName();
    if (containsCI(name ? name : "", wndName)) devs.push_back(d);
  }
  if (devs.empty()) return false;
  // 逐个匹配窗尝试, 直到有一个出帧 (内容窗不行就试框架窗); 首帧偶发不来则重试 1 次
  // (tryCapture 抽到匿名 namespace, 与 shotScreen 共用 SourcePlayer+screenShot 路径)
  bool ok = false;
  for (WinCaptureBase* d : devs) {
    if (ok) break;
    HWND hwnd = d->nativeHwnd();
    // Graphics Capture 抓不了最小化/隐藏窗; 恢复可见后压到 Z 序底部, 不弹到用户面前
    if (hwnd && (!IsWindowVisible(hwnd) || IsIconic(hwnd))) {
      ShowWindow(hwnd, SW_SHOWNOACTIVATE);
      SetWindowPos(hwnd, HWND_BOTTOM, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
      // 等 DWM 合成 + 窗口尺寸生效
      std::this_thread::sleep_for(std::chrono::milliseconds(80));
    }
    for (int attempt = 0; attempt < 2 && !ok; attempt++) {
      ok = tryCapture(d, buffer);
      if (!ok) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    // 成功才回填屏幕原点 + 出帧窗句柄 (用这个出帧窗的坐标/hwnd, 与实际截图一致)
    if (ok && info) {
      fillShotInfo(d, info);  // 按设备 kind 分支: window GetWindowRect / monitor GetMonitorInfo
      fillShotScale(buffer, info);  // 窗口尺寸 ÷ buffer 尺寸 (buffer 可能被归一化)
      info->screenIndex = windowScreenIndex(d->nativeHwnd());
    }
  }
  return ok;
}

// 截图指定显示器(桌面) → buffer (Windows 专用)。
// 按 screenIndex 选 kind==monitor 且 deviceName=="monitor N" 的设备 (与 shotWindow
// 同走 tryCapture); 原点用 GetMonitorInfo 的 rcMonitor 回填 (monitor 无 hwnd)。
bool shotScreen(int32_t screenIndex, IImageBuffer* buffer, WindowShotInfo* info,
                bool showDesktop, bool deferUndo) {
  if (!buffer || screenIndex < 0) return false;
  // showDesktop: 最小化所有窗口露出桌面
  bool desktopShown = false;
  if (showDesktop) {
    desktopShown = ::avox::showDesktop();
    if (desktopShown) {
      // 等桌面动画完成
      std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
  }
  CaptureWindowsMgr mgr;  // 构造即枚举 (勿再 refreshDevices, 会重复 append)
  WinCaptureBase* dev = nullptr;
  std::string target = "monitor " + std::to_string(screenIndex);
  int32_t count = mgr.getDeviceCount();
  for (int32_t i = 0; i < count; i++) {
    WinCaptureBase* d = mgr.getTDevice(i);
    if (!d || d->getDeviceKind() != VDeviceKind::monitor) continue;
    const char* name = d->getDeviceName();
    if (name && containsCI(name, target.c_str())) {
      dev = d;
      break;
    }
  }
  bool ok = false;
  if (dev) {
    for (int attempt = 0; attempt < 2 && !ok; attempt++) {
      ok = tryCapture(dev, buffer);
      if (!ok) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  }
  if (desktopShown && !deferUndo) {
    // 等截图完成后再还原, 避免与 SourcePlayer close 竞争
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    undoDesktop();
  }
  if (ok && info) {
    fillShotInfo(dev, info);
    fillShotScale(buffer, info);  // 显示器尺寸 ÷ buffer 尺寸 (buffer 可能被归一化)
    info->screenIndex = screenIndex;
  }
  return ok;
}

std::vector<std::string> findWindows(const char* wndName) {
  std::vector<std::string> names;
  for (HWND hwnd : findWindowHwnds(wndName)) {
    names.emplace_back(getWindowName(hwnd));
  }
  return names;
}

std::vector<WindowDeviceEntry> listWindowDevices(const char* wndName) {
  std::vector<WindowDeviceEntry> entries;
  CaptureWindowsMgr mgr;  // 构造即枚举 (勿再 refreshDevices, 会重复 append)
  bool matchAll = !wndName || !wndName[0];  // 空 → 全部
  int32_t count = mgr.getDeviceCount();
  for (int32_t i = 0; i < count; i++) {
    WinCaptureBase* d = mgr.getTDevice(i);
    if (!d) continue;
    const char* name = d->getDeviceName();
    std::string title = name ? name : "";
    if (!matchAll && !containsCI(title, wndName)) continue;
    entries.push_back(
        {title, d->getDeviceKind(), d->nativeClass(), d->nativeProcess()});
  }
  return entries;
}

bool activeWindow(const char* wndName) {
  std::vector<HWND> hwnds = findWindowHwnds(wndName);
  if (hwnds.empty()) return false;
  // 前置第一个匹配窗口: 最小化先恢复, 再 Alt 按放绕前台锁 + 置顶 (bringToFront)
  return bringToFront(hwnds.front());
}

bool activeWindowByHwnd(void* hwnd) {
  if (!hwnd) return false;
  // 前置指定句柄窗口 (通常来自 WindowShotInfo::hwnd): 精确到 shotWindow
  // 的出帧窗
  return bringToFront(reinterpret_cast<HWND>(hwnd));
}

// 显示桌面 (最小化所有窗口露出桌面): SendInput 模拟 Win+D toggle。
// 配对 undoDesktop() 再发一次 Win+D 还原。用 toggle 对称操作, 避免状态不一致。
bool showDesktop() {
  INPUT inputs[4] = {};
  inputs[0].type = INPUT_KEYBOARD;
  inputs[0].ki.wVk = VK_LWIN;
  inputs[1].type = INPUT_KEYBOARD;
  inputs[1].ki.wVk = 'D';
  inputs[2].type = INPUT_KEYBOARD;
  inputs[2].ki.wVk = 'D';
  inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
  inputs[3].type = INPUT_KEYBOARD;
  inputs[3].ki.wVk = VK_LWIN;
  inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
  return SendInput(4, inputs, sizeof(INPUT)) == 4;
}

// 还原 showDesktop(): 再发一次 Win+D toggle 回来 (与 showDesktop 配对, 状态对称)。
void undoDesktop() {
  INPUT inputs[4] = {};
  inputs[0].type = INPUT_KEYBOARD;
  inputs[0].ki.wVk = VK_LWIN;
  inputs[1].type = INPUT_KEYBOARD;
  inputs[1].ki.wVk = 'D';
  inputs[2].type = INPUT_KEYBOARD;
  inputs[2].ki.wVk = 'D';
  inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
  inputs[3].type = INPUT_KEYBOARD;
  inputs[3].ki.wVk = VK_LWIN;
  inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
  SendInput(4, inputs, sizeof(INPUT));
}

#else  // 非 Windows: win_capture 不可用
bool shotWindow(const char*, IImageBuffer*, WindowShotInfo*) { return false; }
bool shotScreen(int32_t, IImageBuffer*, WindowShotInfo*, bool, bool) { return false; }
std::vector<std::string> findWindows(const char*) { return {}; }
std::vector<WindowDeviceEntry> listWindowDevices(const char*) { return {}; }
bool activeWindow(const char*) { return false; }
bool activeWindowByHwnd(void*) { return false; }
bool showDesktop() { return false; }
void undoDesktop() {}
#endif

}
