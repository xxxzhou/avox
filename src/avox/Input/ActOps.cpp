#include "avox/Input/ActOps.hpp"

#include <memory>

#include "avox/AvoxInput.h"  // IInputController / createInputController / InputAction / KeyCode

namespace avox {

bool actAt(int32_t screenX, int32_t screenY, InputAction action) {
  if (action == InputAction::none) return true;
  std::unique_ptr<IInputController> ic(createInputController());
  if (!ic) return false;
  switch (action) {
    case InputAction::move:     return ic->moveTo(screenX, screenY);
    case InputAction::dblclick: return ic->doubleClick(screenX, screenY);
    case InputAction::click:    return ic->click(screenX, screenY);
    case InputAction::none:     return true;  // 兜底 (上方已提前返回)
  }
  return ic->click(screenX, screenY);  // 默认 click
}

bool typeTextAt(const char* utf8) {
  if (!utf8 || !utf8[0]) return true;
  std::unique_ptr<IInputController> ic(createInputController());
  if (!ic) return false;
  return ic->typeText(utf8);
}

bool pressKeyAt(KeyCode k, int32_t holdMs) {
  if (k == KeyCode::none) return true;
  std::unique_ptr<IInputController> ic(createInputController());
  if (!ic) return false;
  return ic->press(k, holdMs);
}

bool hotkeyAt(const KeyCode* keys, int32_t n) {
  if (!keys || n <= 0) return false;
  std::unique_ptr<IInputController> ic(createInputController());
  if (!ic) return false;
  return ic->hotkey(keys, n);
}

}
