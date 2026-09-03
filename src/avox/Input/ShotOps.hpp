#pragma once

#include "avox/AvoxInput.h"   // WindowShotInfo / VDeviceKind

#include <string>
#include <vector>

namespace avox {

// 截图命名窗口 → buffer, 并回填屏幕原点信息 (Windows 专用; 非Windows 返回 false)。
// wndName 子串匹配窗口标题(大小写不敏感); buffer 由调用者分配(同 ISurfaceRender::screenShot)。
// 用局部 CaptureWindowsMgr + refreshDevices, 不影响全局设备表。
// 截图成功返回 true 并填 info; 找不到窗口/截图失败返回 false。
bool shotWindow(const char* wndName, IImageBuffer* buffer, WindowShotInfo* info);

// 按 wndName 子串(大小写不敏感)枚举所有匹配窗口的标题 (Windows 专用; 非Windows 返回空)。
// 同样用局部 CaptureWindowsMgr + refreshDevices, 不影响全局设备表。
// wndName 为空/null 时不过滤, 返回全部窗口标题 (供无筛选列举)。
// 返回所有匹配窗口的标题; 同名多窗口都会返回, 不去重。
std::vector<std::string> findWindows(const char* wndName);

// 捕获设备条目 (窗口/显示器): title + 设备来源类别, 供 ops list 列出挑选。
struct WindowDeviceEntry {
  std::string title;     // 设备名 (窗口标题 / 显示器名)
  VDeviceKind kind;      // 设备来源类别 (window/monitor/...)
  std::string winClass;  // Win32 窗口类名 (-l 诊断: frame/content 区分)
  std::string process;   // 拥有窗口的进程 exe (-l 诊断用)
};
// 枚举所有(或 wndName 子串匹配)捕获设备, 返回 title + VDeviceKind (Windows 专用; 非Windows 返回空)。
// 同 findWindows 用局部 CaptureWindowsMgr + refreshDevices, 不影响全局设备表。
// wndName 为空/null 时不过滤, 返回全部设备 (含显示器, 不限于窗口)。
std::vector<WindowDeviceEntry> listWindowDevices(const char* wndName);

// 激活(前置) wndName 匹配的第一个窗口 (Windows 专用; 非Windows 返回 false)。
// 子串匹配同 shotWindow; 最小化会先恢复, 再 SetForegroundWindow 置顶
// (用 SendInput 模拟一次 Alt 按放绕过前台锁)。
// 找到并执行置顶返回 true; 找不到窗口返回 false。
bool activeWindow(const char* wndName);

// 按原生句柄激活(前置)指定窗口 (Windows 专用; 非Windows 返回 false)。
// hwnd 取自 WindowShotInfo::hwnd (shotWindow 实际出帧窗), 比 activeWindow(name)
// 的"首个匹配"更精确 (UWP 内容窗/框架窗成对时区别明显)。实现同 activeWindow:
// 最小化先恢复 + SetForegroundWindow 置顶 (Alt 按放绕前台锁)。无效句柄返回 false。
bool activeWindowByHwnd(void* hwnd);

// 截图指定显示器(桌面) → buffer, 并回填屏幕原点信息 (Windows 专用; 非Windows 返回 false)。
// screenIndex 按 EnumDisplayMonitors 枚举顺序 (0 = 第一台, 通常主屏)。与 shotWindow 同走
// SourcePlayer+screenShot 路径, 仅设备选择 (kind==monitor) 与原点回填 (GetMonitorInfo) 不同;
// 解决 shotWindow 对 monitor 设备 nativeHwnd()=null 致 left/top 丢失的问题。
// showDesktop=true: 截图前最小化所有窗口露出桌面。
// deferUndo=true: 截完后不立刻还原桌面, 由调用方在 act() 后自行调用 undoDesktop()。
//   避免截图→还原→act→再showDesktop 的二次闪烁; 默认 false (立刻还原, 兼容旧调用)。
bool shotScreen(int32_t screenIndex, IImageBuffer* buffer, WindowShotInfo* info,
                bool showDesktop = false, bool deferUndo = false);

// 显示桌面: 最小化所有窗口露出桌面 (Windows 专用; 非Windows 返回 false)。
// 供桌面图标操作前置: 确保桌面可见不被遮挡。实现用 SendInput 模拟 Win+M (最小化所有,
// 确定性; 非 Win+D 的 toggle 二次恢复)。
bool showDesktop();

// 还原桌面: 恢复 showDesktop() 最小化的所有窗口 (Windows 专用; 非Windows 无操作)。
// showDesktop 用 Win+M (单向最小化), 还原用 Win+D toggle。
void undoDesktop();

}
