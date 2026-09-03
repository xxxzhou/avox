#pragma once

#include <stdint.h>

#include "AvoxDef.h"
#include "AvoxMath.h"   // vec2i
#include "AvoxSource.h" // VDeviceKind (WindowEntry)
#include "AvoxVideo.h"  // IImageBuffer (IScreenCapture)

namespace avox {

// ============== 输入注入 (鼠标 / 键盘) ==============
// 跨平台 GUI 输入注入接口, 供 C++ / SWIG 应用做自动化/回放/压测。
// 实现按平台编译期选定:
//   Windows  src/avox_windows/InputController.cpp  (SendInput 全功能)
//   其它平台 src/avox/AvoxInputNoop.cpp             (no-op, 方法返回 false)
// 范本: ITemplateMatcher (AvoxVision.h)。跨 DLL 安全: 签名只用
//   int32_t / 枚举 / const char*, 不传 STL (见 doc/plan/动态加载组件设计.md §6)。

// 鼠标按键
enum class MouseButton { left = 0, right, middle, x1, x2 };

// 统一键码; 平台实现内部映射到 VK_ / XK_ / kVK_
enum class KeyCode {
  none = 0,
  a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q, r, s, t, u, v, w, x, y, z,
  num0, num1, num2, num3, num4, num5, num6, num7, num8, num9,
  f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12,
  space, enter, esc, tab, backspace, del,
  home, end, pageUp, pageDown,
  up, down, left, right,
  shift, ctrl, alt, win,
  capsLock, numLock,
};

// 动作类型: actAt / find* 的 action 参数 (屏幕坐标上的鼠标动作)。
//   none     不动作, 直接返回 true;
//   click    单击 (默认);
//   move     仅移动;
//   dblclick 双击。
// 未知字符串按 click (parseInputAction 维持历史 "其余按 click" 语义)。
#define AVOX_MAP_INPUT_ACTION(XX) \
  XX(none, 0, "none")            \
  XX(click, 1, "click")          \
  XX(move, 2, "move")            \
  XX(dblclick, 3, "dblclick")

enum class InputAction {
#define XX(name, value, str) name = value,
  AVOX_MAP_INPUT_ACTION(XX)
#undef XX
};

class IInputController {
 public:
  virtual ~IInputController() = default;
  // —— 鼠标 ——
  // 以下坐标均为屏幕物理坐标(虚拟桌面坐标系, 多屏时可为负)
  virtual bool moveTo(int32_t x, int32_t y) = 0;             // 移动到目标坐标
  virtual bool moveBy(int32_t dx, int32_t dy) = 0;            // 屏幕相对移动(moveDurationMs>0 时走动画插值)
  virtual bool mouseDown(MouseButton b) = 0;
  virtual bool mouseUp(MouseButton b) = 0;
  virtual bool click(int32_t x, int32_t y, MouseButton b = MouseButton::left) = 0;
  virtual bool doubleClick(int32_t x, int32_t y) = 0;
  // 拖拽: (x1,y1) 按下 -> 分段移动到 (x2,y2) -> 抬起
  virtual bool drag(int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                    MouseButton b = MouseButton::left) = 0;
  virtual bool scroll(int32_t dx, int32_t dy) = 0;            // 横/竖向, 单位=滚轮格
  virtual bool getCursorPos(vec2i* pos) = 0;                    // pos->x/y = 光标屏幕物理坐标
  // —— 键盘 ——
  virtual bool keyDown(KeyCode k) = 0;
  virtual bool keyUp(KeyCode k) = 0;
  virtual bool press(KeyCode k, int32_t holdMs = 0) = 0;       // down -> sleep(holdMs) -> up
  // 组合键(如 Ctrl+C): 按序 down 全部, 再逆序 up
  virtual bool hotkey(const KeyCode* keys, int32_t n) = 0;
  // Unicode 文本输入(绕过键盘布局, 支持中/日文等任意 IME 文本)
  virtual bool typeText(const char* utf8) = 0;
  virtual bool keyState(KeyCode k) = 0;                        // 查询当前是否按下
  // —— 配置 / 查询 ——
  virtual void setMoveDurationMs(int32_t ms) = 0;             // 0=瞬移; >0 拟人插值移动
  virtual void setMoveSteps(int32_t n) = 0;                   // 插值帧数(>1 生效)
  virtual void setKeyDelayMs(int32_t ms) = 0;                 // 击键/连击间隔
  virtual bool screenBounds(vec2i* size) = 0;                 // 虚拟屏尺寸(多屏合并)
  virtual const char* getLastError() = 0;                     // 最近一次错误(内部常量串, 立即读)
};

// ============== 屏幕截图 (Screen Capture) ==============
// 窗口/桌面截图器: 整合 win_capture (枚举/激活/截图/坐标)。识别/输入不内置
// (走 ITextRecognizer/ITemplateMatcher/IInputController 组合, 见 AvoxVision.h)。

// 窗口截图的屏幕信息: 坐标转换与多屏定位用。
// 截图坐标系原点 = 窗口屏幕左上角 (left, top), 故 screen = (left,top) + bufXY。
struct WindowShotInfo {
  int32_t screenIndex = 0;
  int32_t left = 0;
  int32_t top = 0;
  int32_t width = 0;    // 窗口/显示器屏幕尺寸(逻辑像素)
  int32_t height = 0;
  float scaleX = 1.0f;  // 屏幕尺寸 ÷ buffer 尺寸; 截图 buffer 被渲染层归一化(缩放)时 ≠1
  float scaleY = 1.0f;  // 例: 4K 窗口 → 1080p buffer ⇒ scale=2, toScreen 放大回屏幕坐标
  void* hwnd = nullptr;
};

// 窗口枚举条目 (POD; title/winClass/process 为内部指针, 随下次 getWindowCount 失效)
struct WindowEntry {
  const char* title = nullptr;
  VDeviceKind kind = VDeviceKind::none;
  void* hwnd = nullptr;
  const char* winClass = nullptr;
  const char* process = nullptr;
};

// 窗口/桌面截图接口 (跨 DLL 安全: IImageBuffer* + 基本类型 + POD)。
// 实现委托 ShotOps (ops/ScreenCapture.cpp), 不重写 win_capture 逻辑。
class IScreenCapture {
 public:
  virtual ~IScreenCapture() = default;
  virtual int32_t getWindowCount() = 0;               // 枚举 (封装 CaptureWindowsMgr)
  virtual bool getWindowAt(int32_t index, WindowEntry* out) = 0;
  virtual bool activateWindow(const char* titleSub) = 0;
  virtual bool activateHwnd(void* hwnd) = 0;
  virtual bool showDesktop() = 0;
  virtual void undoDesktop() = 0;                         // 还原 showDesktop() 最小化的窗口
  virtual bool setWindow(const char* titleSub) = 0;   // 绑定窗口 + 截图
  virtual bool setScreen(int32_t screenIndex, bool showDesktop) = 0;
  virtual bool refresh() = 0;                          // 当前目标重截
  virtual IImageBuffer* getBuffer() const = 0;        // 托管, 随 ctx
  virtual vec2i toScreen(int32_t bufX, int32_t bufY) const = 0;  // buffer→screen
  virtual IScreenCapture* crop(int32_t x, int32_t y, int32_t w, int32_t h) const = 0;
  virtual bool save(const char* path) const = 0;
  virtual const char* targetName() const = 0;
  virtual int32_t width() const = 0;
  virtual int32_t height() const = 0;
};

extern "C" {
// const char* -> InputAction; nullptr/空/未知 按 click。定义见 avox/Input/Input.cpp。
AVOX_EXPORT InputAction parseInputAction(const char* action);
// InputAction -> const char*。定义见 avox/Input/Input.cpp。
AVOX_EXPORT const char* getInputActionStr(InputAction action);
// 所有合法动作字符串, "|" 连接 (如 "none|click|move|dblclick"), 宏表驱动。
// 供 CLI 帮助/校验提示统一引用, 避免各处硬编码漂移。定义见 avox/Input/Input.cpp。
AVOX_EXPORT const char* inputActionNames();
// action 是否合法动作字符串 (严格匹配宏表; 未知返回 false,
// 不同于 parseInputAction 的 "未知按 click")。定义见 avox/Input/Input.cpp。
AVOX_EXPORT bool checkValidInputAction(const char* action);
// 工厂: 返回当前平台实现; 调用方负责 delete。
// 不可用平台返回 no-op 实例(各方法返回 false), 不返回 nullptr。
AVOX_EXPORT IInputController* createInputController();
// 按窗口标题(子串匹配, name 为 UTF-8)查找顶层窗口, 返回其原生句柄
// (HWND / Window); 找不到或非桌面平台返回 nullptr。供调用方做 scene->屏幕
// 坐标转换(用句柄 ClientToScreen)等使用。
AVOX_EXPORT void* findWindowByName(const char* name);
// 获取当前激活(前台)的顶层窗口句柄(HWND / Window); 没有前台窗口或非桌面
// 平台返回 nullptr。用途同 findWindowByName, 只是查找方式为"当前激活"。
AVOX_EXPORT void* getActiveWindow();
// 获取窗口标题(UTF-8)。hwnd 为 findWindowByName / getActiveWindow 返回的句柄;
// 无效句柄、无标题或非桌面平台返回 nullptr。返回指针指向线程局部缓冲, 下次本
// 线程调用本函数前一直有效, 调用方无需释放。
AVOX_EXPORT const char* getWindowName(void* hwnd);
// buffer 内坐标 → 屏幕坐标 (实现于 ops/Ops.cpp)
AVOX_EXPORT vec2i toScreen(const WindowShotInfo& info, vec2i imgCenter);
// 窗口/桌面截图工厂 (跨 DLL 安全: IImageBuffer* + 基本类型 + POD)
AVOX_EXPORT IScreenCapture* createScreenCapture();
}

}
