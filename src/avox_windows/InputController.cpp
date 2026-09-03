// Windows 输入注入实现: SendInput (鼠标 + 键盘 + Unicode 文本)。
// 坐标: 多显示器虚拟屏幕 0..65535 归一化(MOUSEEVENTF_VIRTUALDESK)。
// 拟人化: humanize=true 时贝塞尔轨迹 + 自然点击保持 + 抖动延迟。
// 声明见 InputController.hpp。仅 WIN32。

#if defined(_WIN32)
#include "InputController.hpp"  // 自带 windows.h + AvoxInput.h + BaseInputController.hpp

#include <algorithm>
#include <cstring>
#include <utility>
#include <vector>

namespace avox {

namespace {
// 统一键码 -> Windows 虚拟键码 (VK_*)
WORD keyCodeToVk(KeyCode k) {
  switch (k) {
    case KeyCode::a: return 'A';   case KeyCode::b: return 'B';   case KeyCode::c: return 'C';
    case KeyCode::d: return 'D';   case KeyCode::e: return 'E';   case KeyCode::f: return 'F';
    case KeyCode::g: return 'G';   case KeyCode::h: return 'H';   case KeyCode::i: return 'I';
    case KeyCode::j: return 'J';   case KeyCode::k: return 'K';   case KeyCode::l: return 'L';
    case KeyCode::m: return 'M';   case KeyCode::n: return 'N';   case KeyCode::o: return 'O';
    case KeyCode::p: return 'P';   case KeyCode::q: return 'Q';   case KeyCode::r: return 'R';
    case KeyCode::s: return 'S';   case KeyCode::t: return 'T';   case KeyCode::u: return 'U';
    case KeyCode::v: return 'V';   case KeyCode::w: return 'W';   case KeyCode::x: return 'X';
    case KeyCode::y: return 'Y';   case KeyCode::z: return 'Z';
    case KeyCode::num0: return '0'; case KeyCode::num1: return '1'; case KeyCode::num2: return '2';
    case KeyCode::num3: return '3'; case KeyCode::num4: return '4'; case KeyCode::num5: return '5';
    case KeyCode::num6: return '6'; case KeyCode::num7: return '7'; case KeyCode::num8: return '8';
    case KeyCode::num9: return '9';
    case KeyCode::f1: return VK_F1;   case KeyCode::f2: return VK_F2;   case KeyCode::f3: return VK_F3;
    case KeyCode::f4: return VK_F4;   case KeyCode::f5: return VK_F5;   case KeyCode::f6: return VK_F6;
    case KeyCode::f7: return VK_F7;   case KeyCode::f8: return VK_F8;   case KeyCode::f9: return VK_F9;
    case KeyCode::f10: return VK_F10; case KeyCode::f11: return VK_F11; case KeyCode::f12: return VK_F12;
    case KeyCode::space: return VK_SPACE;
    case KeyCode::enter: return VK_RETURN;
    case KeyCode::esc: return VK_ESCAPE;
    case KeyCode::tab: return VK_TAB;
    case KeyCode::backspace: return VK_BACK;
    case KeyCode::del: return VK_DELETE;
    case KeyCode::home: return VK_HOME;
    case KeyCode::end: return VK_END;
    case KeyCode::pageUp: return VK_PRIOR;
    case KeyCode::pageDown: return VK_NEXT;
    case KeyCode::up: return VK_UP;
    case KeyCode::down: return VK_DOWN;
    case KeyCode::left: return VK_LEFT;
    case KeyCode::right: return VK_RIGHT;
    case KeyCode::shift: return VK_SHIFT;
    case KeyCode::ctrl: return VK_CONTROL;
    case KeyCode::alt: return VK_MENU;
    case KeyCode::win: return VK_LWIN;
    case KeyCode::capsLock: return VK_CAPITAL;
    case KeyCode::numLock: return VK_NUMLOCK;
    case KeyCode::none:
    default: return 0;
  }
}

// EnumWindows 查找上下文: 按标题(UTF-16)子串匹配, 命中第一个即停
struct FindWndCtx {
  std::wstring needle;
  HWND best;
};
BOOL CALLBACK findWndEnumProc(HWND hwnd, LPARAM lp) {
  auto* ctx = reinterpret_cast<FindWndCtx*>(lp);
  wchar_t title[256] = {0};
  if (GetWindowTextW(hwnd, title, 256) > 0) {
    std::wstring t(title);
    if (t.find(ctx->needle) != std::wstring::npos) {
      ctx->best = hwnd;
      return FALSE;  // 命中即停
    }
  }
  return TRUE;
}
}  // namespace

// ============== WinInputController 私有辅助 ==============
void WinInputController::setError(const char* msg) { lastError = msg ? msg : ""; }

bool WinInputController::sendAbs(int32_t sx, int32_t sy, DWORD flags, DWORD mouseData) {
  int vsLeft = GetSystemMetrics(SM_XVIRTUALSCREEN);
  int vsTop = GetSystemMetrics(SM_YVIRTUALSCREEN);
  int vsW = GetSystemMetrics(SM_CXVIRTUALSCREEN);
  int vsH = GetSystemMetrics(SM_CYVIRTUALSCREEN);
  if (vsW <= 0 || vsH <= 0) { setError("screen metrics invalid"); return false; }
  double kx = 65535.0 / (vsW - 1 > 0 ? vsW - 1 : 1);
  double ky = 65535.0 / (vsH - 1 > 0 ? vsH - 1 : 1);
  INPUT in;
  ZeroMemory(&in, sizeof(INPUT));
  in.type = INPUT_MOUSE;
  in.mi.dx = (LONG)((sx - vsLeft) * kx);
  in.mi.dy = (LONG)((sy - vsTop) * ky);
  in.mi.dwFlags = flags | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
  in.mi.mouseData = mouseData;
  if (SendInput(1, &in, sizeof(INPUT)) != 1) { setError("SendInput mouse failed"); return false; }
  return true;
}

bool WinInputController::sendButton(DWORD flags, DWORD mouseData) {
  INPUT in;
  ZeroMemory(&in, sizeof(INPUT));
  in.type = INPUT_MOUSE;
  in.mi.dwFlags = flags;
  in.mi.mouseData = mouseData;
  if (SendInput(1, &in, sizeof(INPUT)) != 1) { setError("SendInput button failed"); return false; }
  return true;
}

bool WinInputController::buttonImpl(MouseButton b, bool down) {
  DWORD flags = 0; DWORD data = 0;
  switch (b) {
    case MouseButton::left: flags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP; break;
    case MouseButton::right: flags = down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP; break;
    case MouseButton::middle: flags = down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP; break;
    case MouseButton::x1: flags = down ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP; data = XBUTTON1; break;
    case MouseButton::x2: flags = down ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP; data = XBUTTON2; break;
    default: setError("invalid mouse button"); return false;
  }
  return sendButton(flags, data);
}

bool WinInputController::keyImpl(KeyCode k, bool down) {
  WORD vk = keyCodeToVk(k);
  if (vk == 0) { setError("invalid key code"); return false; }
  INPUT in;
  ZeroMemory(&in, sizeof(INPUT));
  in.type = INPUT_KEYBOARD;
  in.ki.wVk = vk;
  in.ki.wScan = 0;
  in.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
  if (SendInput(1, &in, sizeof(INPUT)) != 1) { setError("SendInput key failed"); return false; }
  return true;
}

bool WinInputController::moveToImpl(int32_t x, int32_t y) {
  POINT dst; dst.x = x; dst.y = y;  // 已是屏幕物理坐标
  if (moveDurationMs > 0 && moveSteps > 1) {
    POINT cur;
    if (!GetCursorPos(&cur)) { setError("GetCursorPos failed"); return false; }
    if (humanize) {
      // 贝塞尔曲线: 随机控制点 + 逐点 sendAbs + 微抖动
      int32_t vsL = GetSystemMetrics(SM_XVIRTUALSCREEN);
      int32_t vsT = GetSystemMetrics(SM_YVIRTUALSCREEN);
      int32_t vsW = GetSystemMetrics(SM_CXVIRTUALSCREEN);
      int32_t vsH = GetSystemMetrics(SM_CYVIRTUALSCREEN);
      int32_t cx1, cy1, cx2, cy2;
      makeControlPoints(cur.x, cur.y, dst.x, dst.y, vsL, vsT, vsW, vsH, cx1, cy1, cx2, cy2);
      std::vector<std::pair<int32_t, int32_t>> pts;
      sampleBezier(cur.x, cur.y, cx1, cy1, cx2, cy2, dst.x, dst.y, moveSteps, pts);
      DWORD slice = (DWORD)(moveDurationMs / moveSteps);
      for (int32_t i = 0; i < (int32_t)pts.size(); ++i) {
        // 微抖动: ±1px (终点 i==last 不抖, 保证精确)
        int32_t sx = pts[i].first;
        int32_t sy = pts[i].second;
        if (i < (int32_t)pts.size() - 1) {
          std::uniform_int_distribution<int32_t> d(-1, 1);
          sx += d(rng);
          sy += d(rng);
        }
        if (!sendAbs(sx, sy, MOUSEEVENTF_MOVE, 0)) return false;
        Sleep(jitterMs(slice));
      }
    } else {
      // 线性插值 (原逻辑)
      DWORD slice = (DWORD)(moveDurationMs / moveSteps);
      for (int32_t i = 1; i <= moveSteps; ++i) {
        double t = (double)i / moveSteps;
        int32_t sx = (int32_t)(cur.x + (dst.x - cur.x) * t);
        int32_t sy = (int32_t)(cur.y + (dst.y - cur.y) * t);
        if (!sendAbs(sx, sy, MOUSEEVENTF_MOVE, 0)) return false;
        Sleep(slice);
      }
    }
    return true;
  }
  return sendAbs(dst.x, dst.y, MOUSEEVENTF_MOVE, 0);
}

// ============== WinInputController 公开方法 (IInputController) ==============
bool WinInputController::moveTo(int32_t x, int32_t y) {
  std::lock_guard<std::mutex> g(mtx);
  return moveToImpl(x, y);
}
bool WinInputController::moveBy(int32_t dx, int32_t dy) {
  std::lock_guard<std::mutex> g(mtx);
  POINT cur;
  if (!GetCursorPos(&cur)) { setError("GetCursorPos failed"); return false; }
  // 有动画配置时走 moveToImpl（贝塞尔/线性插值），否则瞬移
  if (moveDurationMs > 0 && moveSteps > 1) {
    return moveToImpl(cur.x + dx, cur.y + dy);
  }
  return sendAbs(cur.x + dx, cur.y + dy, MOUSEEVENTF_MOVE, 0);
}
bool WinInputController::mouseDown(MouseButton b) { std::lock_guard<std::mutex> g(mtx); return buttonImpl(b, true); }
bool WinInputController::mouseUp(MouseButton b) { std::lock_guard<std::mutex> g(mtx); return buttonImpl(b, false); }
bool WinInputController::click(int32_t x, int32_t y, MouseButton b) {
  std::lock_guard<std::mutex> g(mtx);
  if (!moveToImpl(x, y)) return false;
  if (!buttonImpl(b, true)) return false;
  int32_t hold = clickHold();  // humanize 时随机 40~90ms, 否则 0
  if (hold > 0) Sleep(hold);
  return buttonImpl(b, false);
}
bool WinInputController::doubleClick(int32_t x, int32_t y) {
  std::lock_guard<std::mutex> g(mtx);
  if (!moveToImpl(x, y)) return false;
  if (!buttonImpl(MouseButton::left, true) || !buttonImpl(MouseButton::left, false)) return false;
  // 两次 click 间隔: humanize 时随机 ~50ms; 否则取系统双击时间的一半 (默认 ~250ms),
  // 确保 Explorer 桌面图标等能正确识别双击 (10ms 太短, Explorer 不认)
  int32_t gap = humanize ? jitterMs(50) : (GetDoubleClickTime() / 2);
  Sleep(gap);
  if (!buttonImpl(MouseButton::left, true) || !buttonImpl(MouseButton::left, false)) return false;
  return true;
}
bool WinInputController::drag(int32_t x1, int32_t y1, int32_t x2, int32_t y2, MouseButton b) {
  std::lock_guard<std::mutex> g(mtx);
  if (!moveToImpl(x1, y1)) return false;
  if (!buttonImpl(b, true)) return false;
  int32_t steps = moveSteps > 1 ? moveSteps : 8;
  POINT a; a.x = x1; a.y = y1;  // 屏幕物理坐标
  POINT d; d.x = x2; d.y = y2;
  if (humanize) {
    // 贝塞尔拖拽
    int32_t vsL = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int32_t vsT = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int32_t vsW = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int32_t vsH = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    int32_t cx1, cy1, cx2, cy2;
    makeControlPoints(a.x, a.y, d.x, d.y, vsL, vsT, vsW, vsH, cx1, cy1, cx2, cy2);
    std::vector<std::pair<int32_t, int32_t>> pts;
    sampleBezier(a.x, a.y, cx1, cy1, cx2, cy2, d.x, d.y, steps, pts);
    DWORD slice = moveDurationMs > 0 ? (DWORD)(moveDurationMs / steps) : 4;
    for (int32_t i = 0; i < (int32_t)pts.size(); ++i) {
      if (!sendAbs(pts[i].first, pts[i].second, MOUSEEVENTF_MOVE, 0)) return false;
      Sleep(jitterMs(slice));
    }
  } else {
    // 线性拖拽 (原逻辑)
    DWORD slice = moveDurationMs > 0 ? (DWORD)(moveDurationMs / steps) : 4;
    for (int32_t i = 1; i <= steps; ++i) {
      double t = (double)i / steps;
      int32_t sx = (int32_t)(a.x + (d.x - a.x) * t);
      int32_t sy = (int32_t)(a.y + (d.y - a.y) * t);
      if (!sendAbs(sx, sy, MOUSEEVENTF_MOVE, 0)) return false;
      Sleep(slice);
    }
  }
  return buttonImpl(b, false);
}
bool WinInputController::scroll(int32_t dx, int32_t dy) {
  std::lock_guard<std::mutex> g(mtx);
  if (dy != 0 && !sendButton(MOUSEEVENTF_WHEEL, (DWORD)(dy * WHEEL_DELTA))) return false;
  if (dx != 0 && !sendButton(MOUSEEVENTF_HWHEEL, (DWORD)(dx * WHEEL_DELTA))) return false;
  return true;
}
bool WinInputController::getCursorPos(vec2i* pos) {
  std::lock_guard<std::mutex> g(mtx);
  POINT cur;
  if (!GetCursorPos(&cur)) { setError("GetCursorPos failed"); return false; }
  if (pos) { pos->x = cur.x; pos->y = cur.y; }  // 屏幕物理坐标
  return true;
}
bool WinInputController::keyDown(KeyCode k) { std::lock_guard<std::mutex> g(mtx); return keyImpl(k, true); }
bool WinInputController::keyUp(KeyCode k) { std::lock_guard<std::mutex> g(mtx); return keyImpl(k, false); }
bool WinInputController::press(KeyCode k, int32_t holdMs) {
  std::lock_guard<std::mutex> g(mtx);
  if (!keyImpl(k, true)) return false;
  if (holdMs > 0) Sleep(jitterMs(holdMs));
  return keyImpl(k, false);
}
bool WinInputController::hotkey(const KeyCode* keys, int32_t n) {
  std::lock_guard<std::mutex> g(mtx);
  if (!keys || n <= 0) { setError("hotkey empty"); return false; }
  for (int32_t i = 0; i < n; ++i) {
    if (!keyImpl(keys[i], true)) return false;
    Sleep(jitterMs(keyDelayMs > 0 ? keyDelayMs : 10));
  }
  for (int32_t i = n - 1; i >= 0; --i) {
    if (!keyImpl(keys[i], false)) return false;
    Sleep(jitterMs(keyDelayMs > 0 ? keyDelayMs : 10));
  }
  return true;
}
bool WinInputController::typeText(const char* utf8) {
  std::lock_guard<std::mutex> g(mtx);
  if (!utf8) { setError("typeText null"); return false; }
  int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
  if (wlen <= 1) return true;  // 空串
  std::vector<wchar_t> w((size_t)wlen);
  MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w.data(), wlen);
  for (int i = 0; i < wlen - 1; ++i) {
    INPUT in[2];
    ZeroMemory(in, sizeof(in));
    in[0].type = INPUT_KEYBOARD; in[0].ki.wScan = w[i]; in[0].ki.dwFlags = KEYEVENTF_UNICODE;
    in[1].type = INPUT_KEYBOARD; in[1].ki.wScan = w[i]; in[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
    if (SendInput(2, in, sizeof(INPUT)) != 2) { setError("SendInput unicode failed"); return false; }
    if (humanize) Sleep(jitterMs(keyDelayMs > 0 ? keyDelayMs : 15));
  }
  return true;
}
bool WinInputController::keyState(KeyCode k) {
  WORD vk = keyCodeToVk(k);
  if (vk == 0) return false;
  return (GetAsyncKeyState(vk) & 0x8000) != 0;
}
void WinInputController::setMoveDurationMs(int32_t ms) { std::lock_guard<std::mutex> g(mtx); moveDurationMs = ms > 0 ? ms : 0; }
void WinInputController::setMoveSteps(int32_t n) { std::lock_guard<std::mutex> g(mtx); moveSteps = n > 1 ? n : 16; }
void WinInputController::setKeyDelayMs(int32_t ms) { std::lock_guard<std::mutex> g(mtx); keyDelayMs = ms > 0 ? ms : 0; }
bool WinInputController::screenBounds(vec2i* size) {
  std::lock_guard<std::mutex> g(mtx);
  int w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
  int h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
  if (w <= 0 || h <= 0) { setError("screen metrics invalid"); return false; }
  if (size) { size->x = w; size->y = h; }
  return true;
}
const char* WinInputController::getLastError() { std::lock_guard<std::mutex> g(mtx); return lastError.c_str(); }

// ============== 工厂 ==============
AVOX_EXPORT IInputController* createInputController() { return new WinInputController(); }

// 按窗口标题(UTF-8 子串)查找顶层窗口句柄; 找不到返回 nullptr。
AVOX_EXPORT void* findWindowByName(const char* name) {
  if (!name || !name[0]) return nullptr;
  int wlen = MultiByteToWideChar(CP_UTF8, 0, name, -1, nullptr, 0);
  if (wlen <= 0) return nullptr;
  std::wstring wname((size_t)wlen, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, name, -1, wname.data(), wlen);
  if (!wname.empty() && wname.back() == L'\0') wname.pop_back();  // 去尾 null
  FindWndCtx ctx;
  ctx.needle = wname;
  ctx.best = nullptr;
  EnumWindows(findWndEnumProc, reinterpret_cast<LPARAM>(&ctx));
  return ctx.best;
}

// 获取当前激活(前台)的顶层窗口句柄; 无前台窗口返回 nullptr。
AVOX_EXPORT void* getActiveWindow() {
  HWND hwnd = GetForegroundWindow();
  return hwnd ? reinterpret_cast<void*>(hwnd) : nullptr;
}

// 获取窗口标题(UTF-8); 无效句柄或无标题返回 nullptr。
// 返回指针指向线程局部缓冲, 下次本线程调用本函数前有效。
AVOX_EXPORT const char* getWindowName(void* hwnd) {
  if (!hwnd) return nullptr;
  HWND h = reinterpret_cast<HWND>(hwnd);
  int wlen = GetWindowTextLengthW(h);
  if (wlen <= 0) return nullptr;
  std::wstring wname((size_t)wlen + 1, L'\0');
  int got = GetWindowTextW(h, wname.data(), wlen + 1);
  if (got <= 0) return nullptr;
  int ulen = WideCharToMultiByte(CP_UTF8, 0, wname.c_str(), got, nullptr, 0, nullptr, nullptr);
  if (ulen <= 0) return nullptr;
  thread_local std::string utf8;
  utf8.resize((size_t)ulen);
  WideCharToMultiByte(CP_UTF8, 0, wname.c_str(), got, utf8.data(), ulen, nullptr, nullptr);
  return utf8.c_str();
}

}

#endif  // defined(_WIN32)
