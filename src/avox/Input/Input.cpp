// InputAction <-> const char* 转换 (X-macro 驱动), 供 actAt / find* 解析 action。
// InputAction 枚举与 AVOX_MAP_INPUT_ACTION 宏定义于 avox/AvoxInput.h, 故本文件不含
// 枚举定义, 仅做字符串映射。
// parseKeyCode / parseKeyList: 键名字符串 -> KeyCode, 供 ops -K/-H 及 step 使用。
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

#include "avox/AvoxInput.h"

namespace avox {

// buffer 内坐标 → 屏幕坐标: 截图原点即窗口屏幕左上角。
// 缩放因子: 截图 buffer 可能被渲染层归一化(如 4K 窗口 → 1080p buffer),
// 屏幕坐标 = 窗口左上 + buffer 坐标 × (窗口尺寸 / buffer 尺寸)。默认 scale=1(1:1)。
// 公共 helper: 任何组合 shot + 屏幕坐标的 op 都可复用 (findText/findImage 用它做转换)。
vec2i toScreen(const WindowShotInfo& info, vec2i imgCenter) {
  vec2i s;
  s.x = info.left + static_cast<int32_t>(imgCenter.x * info.scaleX);
  s.y = info.top + static_cast<int32_t>(imgCenter.y * info.scaleY);
  return s;
}

// const char* -> InputAction; nullptr/空/未知 按 click (维持历史 "其余按 click" 语义)。
InputAction parseInputAction(const char* action) {
  if (!action) return InputAction::click;
#define XX(name, value, str)           \
  if (std::strcmp(action, str) == 0) { \
    return InputAction::name;          \
  }
  AVOX_MAP_INPUT_ACTION(XX)
#undef XX
  return InputAction::click;
}

// InputAction -> const char*。
const char* getInputActionStr(InputAction action) {
  switch (action) {
#define XX(name, value, str) \
  case InputAction::name:    \
    return str;
    AVOX_MAP_INPUT_ACTION(XX)
#undef XX
    default:
      return "click";
  }
}

// 所有合法动作字符串, "|" 连接 (如 "none|click|move|dblclick"), 宏表驱动。
// 供 CLI 帮助/校验提示统一引用, 避免各处硬编码漂移。
const char* inputActionNames() {
  static std::string s;
  if (!s.empty()) return s.c_str();
  bool first = true;
#define XX(name, value, str) \
  {                          \
    if (!first) s += "|";    \
    s += str;                \
    first = false;           \
  }
  AVOX_MAP_INPUT_ACTION(XX)
#undef XX
  return s.c_str();
}

// action 是否合法动作字符串 (严格匹配宏表; 未知返回 false,
// 不同于 parseInputAction 的 "未知按 click")。
bool checkValidInputAction(const char* action) {
  if (!action) return false;
#define XX(name, value, str)           \
  if (std::strcmp(action, str) == 0) { \
    return true;                       \
  }
  AVOX_MAP_INPUT_ACTION(XX)
#undef XX
  return false;
}

// 键名字符串 -> KeyCode
KeyCode parseKeyCode(const char* s) {
  if (!s || !s[0]) return KeyCode::none;
  std::string n = s;
  for (auto& c : n) c = (char)tolower((unsigned char)c);
  if (n.size() == 1) {
    char c = n[0];
    if (c >= 'a' && c <= 'z') return (KeyCode)((int)KeyCode::a + (c - 'a'));
    if (c >= '0' && c <= '9') return (KeyCode)((int)KeyCode::num0 + (c - '0'));
  }
  if (n[0] == 'f') {  // f1..f12
    int idx = atoi(n.c_str() + 1);
    if (idx >= 1 && idx <= 12) return (KeyCode)((int)KeyCode::f1 + (idx - 1));
  }
  if (n == "space") return KeyCode::space;
  if (n == "enter" || n == "return") return KeyCode::enter;
  if (n == "esc" || n == "escape") return KeyCode::esc;
  if (n == "tab") return KeyCode::tab;
  if (n == "backspace" || n == "back") return KeyCode::backspace;
  if (n == "del" || n == "delete") return KeyCode::del;
  if (n == "home") return KeyCode::home;
  if (n == "end") return KeyCode::end;
  if (n == "pageup" || n == "pgup") return KeyCode::pageUp;
  if (n == "pagedown" || n == "pgdn") return KeyCode::pageDown;
  if (n == "up") return KeyCode::up;
  if (n == "down") return KeyCode::down;
  if (n == "left") return KeyCode::left;
  if (n == "right") return KeyCode::right;
  if (n == "shift") return KeyCode::shift;
  if (n == "ctrl" || n == "control") return KeyCode::ctrl;
  if (n == "alt") return KeyCode::alt;
  if (n == "win" || n == "meta" || n == "super") return KeyCode::win;
  if (n == "capslock" || n == "caps") return KeyCode::capsLock;
  if (n == "numlock") return KeyCode::numLock;
  return KeyCode::none;
}

// 逗号分隔键名列表 -> KeyCode 数组
std::vector<KeyCode> parseKeyList(const std::string& s) {
  std::vector<KeyCode> out;
  std::string cur;
  for (char c : s) {
    if (c == ',') {
      if (!cur.empty()) { out.push_back(parseKeyCode(cur.c_str())); cur.clear(); }
    } else {
      cur.push_back(c);
    }
  }
  if (!cur.empty()) out.push_back(parseKeyCode(cur.c_str()));
  return out;
}

}
