#include "CmdInput.hpp"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "avox/AvoxInput.h"
#include "avox/Input/ActOps.hpp"       // parseKeyCode / parseKeyList
#include "avox/Avox.hpp"          // getAvoxPath
#include "avox/vision/BaseInputController.hpp"  // cross-cast 拟人化设置
#include "avox_cmd/CmdHelper.hpp"  // ensureDir

namespace avox {

// 用法 (扁平: -a 选动作; type/key/hotkey 也可由 -t/-k/-keys 隐含):
//   input -a cursor                      打印当前光标
//   input -a bounds                      打印屏幕(虚拟桌面)尺寸
//   input -a move   -x 100 -y 200        移动到 (x,y)
//   input -a moveby -dx 50 -dy 0         相对移动
//   input -a click  -x 100 -y 200 [-b right]  点击 (默认左键)
//   input -a dblclick -x 100 -y 200      双击
//   input -a drag   -x 100 -y 100 -x2 300 -y2 100 [-b left]  拖拽
//   input -a scroll -dx 0 -dy -3         滚轮 (单位=格, 负=下/右)
//   input -t "hello 你好"                输入文本 (= -a type, Unicode)
//   input -k enter [-hold 500]           按键 (= -a key, 可带按住毫秒)
//   input -keys ctrl,c                   组合键 (= -a hotkey)
//   通用: -d <ms> 移动时长(拟人, 0=瞬移)  坐标=屏幕物理坐标(虚拟桌面)
// 拟人: -humanize 贝塞尔轨迹+自然点击+抖动  -seed <n> 可复现种子  -clickhold <ms> 点击按住
// 键名: a-z 0-9 f1-f12 space enter esc tab backspace del home end pageup pagedown
//       up down left right shift ctrl alt win capslock numlock

namespace {
// 字符串 -> MouseButton
MouseButton parseMouseButton(const char* s) {
  if (!s || !s[0]) return MouseButton::left;
  std::string n = s;
  for (auto& c : n) c = (char)tolower((unsigned char)c);
  if (n == "right") return MouseButton::right;
  if (n == "middle" || n == "mid") return MouseButton::middle;
  if (n == "x1") return MouseButton::x1;
  if (n == "x2") return MouseButton::x2;
  return MouseButton::left;
}

void printInputUsage() {
  printf("input -a <动作> [options]   (无位置子命令; 动作由 -a 选, type/key/hotkey 可由 -t/-k/-keys 隐含)\n"
         "  -a cursor | -a bounds\n"
         "  -a move -x -y | -a moveby -dx -dy\n"
         "  -a click -x -y [-b] | -a dblclick -x -y\n"
         "  -a drag -x -y -x2 -y2 [-b]\n"
         "  -a scroll -dx -dy\n"
         "  -t <text> (type) | -k <key> [-hold ms] (key) | -keys <a,b,c> (hotkey)\n"
         "  通用: -d <ms 移动时长>  (坐标=屏幕物理坐标)\n"
         "  拟人: -humanize -seed <n> -clickhold <ms>\n");
}
}  // namespace

Command cmdInput() {
  Command cmd;
  cmd.name = "input";
  cmd.desc = "模拟鼠标/键盘: move/click/dblclick/drag/scroll/type/key/hotkey";
  cmd.parser.addArg({"-a", "--action", ArgType::String, false,
                     "动作: cursor|bounds|move|moveby|click|dblclick|drag|scroll|type|key|hotkey "
                     "(type/key/hotkey 可省略, 由 -t/-k/-keys 隐含)", ""});
  cmd.parser.addArg({"-x", "", ArgType::Int, false, "X 坐标", "0"});
  cmd.parser.addArg({"-y", "", ArgType::Int, false, "Y 坐标", "0"});
  cmd.parser.addArg({"-x2", "", ArgType::Int, false, "拖拽目标 X", "0"});
  cmd.parser.addArg({"-y2", "", ArgType::Int, false, "拖拽目标 Y", "0"});
  cmd.parser.addArg({"-dx", "", ArgType::Int, false, "横向增量(滚轮/相对移动)", "0"});
  cmd.parser.addArg({"-dy", "", ArgType::Int, false, "纵向增量(滚轮/相对移动)", "0"});
  cmd.parser.addArg({"-b", "--button", ArgType::String, false, "鼠标键 left|right|middle|x1|x2", "left"});
  cmd.parser.addArg({"-t", "--text", ArgType::String, false, "type 输入的文本", ""});
  cmd.parser.addArg({"-k", "--key", ArgType::String, false, "key 按键名", ""});
  cmd.parser.addArg({"-keys", "", ArgType::String, false, "hotkey 组合键, 逗号分隔", ""});
  cmd.parser.addArg({"-hold", "", ArgType::Int, false, "key 按住毫秒", "0"});
  cmd.parser.addArg({"-d", "--duration", ArgType::Int, false, "鼠标移动时长(拟人, 0=瞬移)", "0"});
  cmd.parser.addArg({"-humanize", "", ArgType::Boolean, false, "拟人化(贝塞尔轨迹+自然点击+抖动)", ""});
  cmd.parser.addArg({"-seed", "", ArgType::Int, false, "拟人化种子(0=随机, >0=可复现)", "0"});
  cmd.parser.addArg({"-clickhold", "", ArgType::Int, false, "点击按住毫秒(0=humanize时随机)", "0"});
  cmd.run = [](const ParsedArgs& args) -> int {
    std::unique_ptr<IInputController> ic(createInputController());
    if (!ic) { printf("createInputController 失败\n"); return 1; }
    if (args.has("duration")) ic->setMoveDurationMs(args.getInt("duration"));
    // 拟人化配置: cross-cast 到 BaseInputController (同 addTextRecognizerOb 模式)
    BaseInputController* bic = dynamic_cast<BaseInputController*>(ic.get());
    if (bic) {
      if (args.getBool("humanize")) bic->setHumanize(true);
      int32_t seed = args.getInt("seed");
      if (seed > 0) bic->setJitterSeed((uint32_t)seed);
      int32_t ch = args.getInt("clickhold");
      if (ch > 0) bic->setClickHoldMs(ch);
    }
    // 动作: -a 显式优先; 否则 type/key/hotkey 由 -t/-k/-keys 隐含
    std::string action = args.getString("action");
    if (action.empty()) {
      if (!args.getString("text").empty())
        action = "type";
      else if (!args.getString("key").empty())
        action = "key";
      else if (!args.getString("keys").empty())
        action = "hotkey";
    }
    if (action.empty()) {
      printf("缺少动作: 用 -a <动作>, 或 -t/-k/-keys 隐含 type/key/hotkey\n");
      printInputUsage();
      return 1;
    }
    MouseButton btn = parseMouseButton(args.getString("button", "left").c_str());
    bool ok = false;
    if (action == "cursor") {
      vec2i p{0, 0};
      ok = ic->getCursorPos(&p);
      if (ok) printf("cursor: (%d, %d)\n", p.x, p.y);
    } else if (action == "bounds") {
      vec2i s{0, 0};
      ok = ic->screenBounds(&s);
      if (ok) printf("screen: %d x %d\n", s.x, s.y);
    } else if (action == "move") {
      ok = ic->moveTo(args.getInt("x"), args.getInt("y"));
    } else if (action == "moveby") {
      ok = ic->moveBy(args.getInt("dx"), args.getInt("dy"));
    } else if (action == "click") {
      ok = ic->click(args.getInt("x"), args.getInt("y"), btn);
    } else if (action == "dblclick") {
      ok = ic->doubleClick(args.getInt("x"), args.getInt("y"));
    } else if (action == "drag") {
      ok = ic->drag(args.getInt("x"), args.getInt("y"), args.getInt("x2"), args.getInt("y2"), btn);
    } else if (action == "scroll") {
      ok = ic->scroll(args.getInt("dx"), args.getInt("dy"));
    } else if (action == "type") {
      std::string t = args.getString("text");
      ok = ic->typeText(t.c_str());
    } else if (action == "key") {
      KeyCode k = parseKeyCode(args.getString("key").c_str());
      ok = ic->press(k, args.getInt("hold"));
    } else if (action == "hotkey") {
      auto keys = parseKeyList(args.getString("keys"));
      if (keys.empty()) { printf("hotkey 需 -keys <a,b,c>\n"); return 1; }
      ok = ic->hotkey(keys.data(), (int32_t)keys.size());
    } else {
      printf("未知 action: %s\n", action.c_str());
      printInputUsage();
      return 1;
    }
    if (!ok) { printf("FAIL: %s\n", ic->getLastError()); return 1; }
    printf("OK\n");
    return 0;
  };
  return cmd;
}

}
