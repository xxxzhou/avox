// Windows 输入注入实现声明: WinInputController (SendInput 鼠标/键盘/Unicode)。
// 实现见 InputController.cpp。仅 WIN32, 由 add_sub_path(avox_windows) 收集。
#pragma once

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX 1
#endif
#include <windows.h>

#include <mutex>
#include <string>

#include "avox/AvoxInput.h"
#include "avox/vision/BaseInputController.hpp"

namespace avox {

// 文件内辅助 keyCodeToVk / FindWndCtx / findWndEnumProc 见 .cpp, 不暴露。
class WinInputController : public BaseInputController {
 public:
  WinInputController() = default;
  ~WinInputController() override = default;
 private:
  int32_t moveDurationMs = 0;      // 0=瞬移
  int32_t moveSteps = 16;          // 插值帧数
  int32_t keyDelayMs = 0;          // 击键间隔
  std::string lastError;
  std::mutex mtx;
  void setError(const char* msg);
  bool sendAbs(int32_t sx, int32_t sy, DWORD flags, DWORD mouseData);  // 屏幕坐标->归一化->SendInput
  bool sendButton(DWORD flags, DWORD mouseData);         // 仅按键/滚轮事件(不动光标)
  bool buttonImpl(MouseButton b, bool down);
  bool keyImpl(KeyCode k, bool down);
  bool moveToImpl(int32_t x, int32_t y);                 // 不加锁, 供 click/drag 组合复用
 public:
  // IInputController
  bool moveTo(int32_t x, int32_t y) override;
  bool moveBy(int32_t dx, int32_t dy) override;
  bool mouseDown(MouseButton b) override;
  bool mouseUp(MouseButton b) override;
  bool click(int32_t x, int32_t y, MouseButton b) override;
  bool doubleClick(int32_t x, int32_t y) override;
  bool drag(int32_t x1, int32_t y1, int32_t x2, int32_t y2, MouseButton b) override;
  bool scroll(int32_t dx, int32_t dy) override;
  bool getCursorPos(vec2i* pos) override;
  bool keyDown(KeyCode k) override;
  bool keyUp(KeyCode k) override;
  bool press(KeyCode k, int32_t holdMs) override;
  bool hotkey(const KeyCode* keys, int32_t n) override;
  bool typeText(const char* utf8) override;
  bool keyState(KeyCode k) override;
  void setMoveDurationMs(int32_t ms) override;
  void setMoveSteps(int32_t n) override;
  void setKeyDelayMs(int32_t ms) override;
  bool screenBounds(vec2i* size) override;
  const char* getLastError() override;
};

}
#endif  // defined(_WIN32)
