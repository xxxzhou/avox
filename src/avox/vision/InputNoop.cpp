// 非 Windows 平台的 no-op 兜底实现, 保证 createInputController 链接通过。
// 各方法返回 false + getLastError() 提示"本平台未实现"。后续 Linux(X11 XTest)/
// macOS(CGEvent) 各自在 src/avox_linux|avox_apple 下提供真实实现时, 把本文件的
// 守护条件收紧即可 (如 #if !defined(_WIN32) && !defined(__LINUX__))。

#include "avox/AvoxInput.h"
#include "avox/vision/BaseInputController.hpp"

#if !defined(_WIN32)
#include <string>

namespace avox {

namespace {
class NoopInputController : public BaseInputController {
 public:
  NoopInputController() = default;
  ~NoopInputController() override = default;
 private:
  std::string lastError;
  void note() { lastError = "input control not implemented on this platform"; }
 public:
  bool moveTo(int32_t, int32_t) override { note(); return false; }
  bool moveBy(int32_t, int32_t) override { note(); return false; }
  bool mouseDown(MouseButton) override { note(); return false; }
  bool mouseUp(MouseButton) override { note(); return false; }
  bool click(int32_t, int32_t, MouseButton) override { note(); return false; }
  bool doubleClick(int32_t, int32_t) override { note(); return false; }
  bool drag(int32_t, int32_t, int32_t, int32_t, MouseButton) override { note(); return false; }
  bool scroll(int32_t, int32_t) override { note(); return false; }
  bool getCursorPos(vec2i*) override { note(); return false; }
  bool keyDown(KeyCode) override { note(); return false; }
  bool keyUp(KeyCode) override { note(); return false; }
  bool press(KeyCode, int32_t) override { note(); return false; }
  bool hotkey(const KeyCode*, int32_t) override { note(); return false; }
  bool typeText(const char*) override { note(); return false; }
  bool keyState(KeyCode) override { note(); return false; }
  void setMoveDurationMs(int32_t) override {}
  void setMoveSteps(int32_t) override {}
  void setKeyDelayMs(int32_t) override {}
  bool screenBounds(vec2i*) override { note(); return false; }
  const char* getLastError() override { return lastError.c_str(); }
};
}  // namespace

AVOX_EXPORT IInputController* createInputController() { return new NoopInputController(); }

// 非 Windows: 无法枚举窗口, 返回 nullptr。
AVOX_EXPORT void* findWindowByName(const char*) { return nullptr; }

// 非 Windows: 无前台窗口概念, 返回 nullptr。
AVOX_EXPORT void* getActiveWindow() { return nullptr; }

// 非 Windows: 无法读取窗口标题, 返回 nullptr。
AVOX_EXPORT const char* getWindowName(void*) { return nullptr; }

}

#endif  // !defined(_WIN32)
