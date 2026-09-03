#pragma once

#include <string>
#include <vector>

#include "avox/AvoxInput.h"  // InputAction / KeyCode / WindowShotInfo

namespace avox {

// 在屏幕坐标执行鼠标动作 (纯输入: IInputController, 屏幕物理坐标/虚拟桌面)。
// action: none 直接返回 true; click/move/dblclick 执行对应鼠标动作。
bool actAt(int32_t screenX, int32_t screenY, InputAction action);

// 键盘输入 (纯输入: IInputController, 无需屏幕坐标)。
// typeTextAt: Unicode 文本输入 (绕过键盘布局, 支持中文等)。
bool typeTextAt(const char* utf8);
// pressKeyAt: 按键 (enter/esc/tab 等); holdMs>0 时按住指定毫秒。
bool pressKeyAt(KeyCode k, int32_t holdMs = 0);
// hotkeyAt: 组合键 (如 Ctrl+C): 按序 down 全部, 再逆序 up。
bool hotkeyAt(const KeyCode* keys, int32_t n);

// 键名字符串 -> KeyCode (如 "enter"->enter, "a"->a, "f1"->f1); 未知返回 none。
KeyCode parseKeyCode(const char* s);
// 逗号分隔键名列表 -> KeyCode 数组 (如 "ctrl,c" -> [ctrl, c])。
std::vector<KeyCode> parseKeyList(const std::string& s);

}
