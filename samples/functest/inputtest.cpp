/**
 * @file inputtest.cpp
 * @brief 输入注入测试 (IInputController) —— 点击 / 打字 / 拖拽 真实闭环
 *
 * 会真实移动鼠标、点击、键盘输入! 运行前请:
 *   1. 打开记事本(或任意文本框), 在编辑区点一下让光标进去并获得焦点;
 *   2. 运行本程序, 在倒计时内不要动鼠标/键盘;
 *   3. 观察鼠标移动 -> 点击 -> 文字输入 -> 小拖拽选中文本。
 *
 * 用法:
 *   inputtest                       # 5 秒倒计时后执行
 *   inputtest -n                    # dry-run, 只打印动作不真正注入
 *   inputtest -w 8                  # 倒计时 8 秒
 *   inputtest -t "你好 avox"          # 自定义输入文本
 *   inputtest -d 300                # 鼠标移动时长 300ms (拟人)
 */

#include <chrono>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "avox/AvoxInput.h"

using namespace avox;

struct Opts {
  bool dryRun = false;
  int waitSec = 5;
  int moveDurationMs = 250;
  std::string text = "Hello from avox IInputController!";
};

static void printHelp(const char* prog) {
  std::cout << "用法: " << prog << " [选项]\n\n"
            << "  真实注入鼠标/键盘! 运行前先在记事本编辑区点一下放好光标。\n\n"
            << "选项:\n"
            << "  -n             dry-run, 只打印动作不注入\n"
            << "  -w <秒>        倒计时秒数 (默认 5)\n"
            << "  -d <ms>        鼠标移动时长 (拟人插值, 默认 250; 0=瞬移)\n"
            << "  -t <文本>      要输入的文本 (默认英文示例)\n"
            << "  -h             帮助\n"
            << std::endl;
}

static bool parseArgs(int argc, char* argv[], Opts& opts) {
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "-h") { printHelp(argv[0]); return false; }
    if (a == "-n") { opts.dryRun = true; continue; }
    if (a == "-w" && i + 1 < argc) { opts.waitSec = std::stoi(argv[++i]); continue; }
    if (a == "-d" && i + 1 < argc) { opts.moveDurationMs = std::stoi(argv[++i]); continue; }
    if (a == "-t" && i + 1 < argc) { opts.text = argv[++i]; continue; }
    std::cerr << "未知参数: " << a << "\n";
    printHelp(argv[0]);
    return false;
  }
  return true;
}

static void sleepMs(int ms) {
  if (ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// 打印动作; dryRun 时只打印不执行, 返回是否真正执行了
static bool act(IInputController* ic, const Opts& opts, const char* desc,
                const std::function<bool()>& fn) {
  std::cout << "  -> " << desc;
  if (opts.dryRun) { std::cout << "  [dry-run]\n"; return false; }
  bool ok = fn();
  std::cout << (ok ? "  OK" : "  FAIL") << "\n";
  return ok;
}

int main(int argc, char* argv[]) {
  std::cout << "=== 输入注入测试 (IInputController) ===\n";
  Opts opts;
  if (!parseArgs(argc, argv, opts)) return 0;

  std::unique_ptr<IInputController> ic(createInputController());
  if (!ic) {
    std::cerr << "createInputController 返回 nullptr\n";
    return 1;
  }
  ic->setMoveDurationMs(opts.moveDurationMs);
  ic->setMoveSteps(20);

  // 读状态: 屏幕尺寸 + 当前光标
  vec2i size{0, 0};
  vec2i start{0, 0};
  if (ic->screenBounds(&size)) {
    std::cout << "屏幕(虚拟桌面): " << size.x << " x " << size.y << "\n";
  } else {
    std::cout << "screenBounds 失败: " << ic->getLastError() << "\n";
  }
  if (ic->getCursorPos(&start)) {
    std::cout << "当前光标: (" << start.x << ", " << start.y << ")\n";
  } else {
    std::cout << "getCursorPos 失败: " << ic->getLastError() << "\n";
  }

  // 倒计时 (给用户把焦点放好)
  if (!opts.dryRun) {
    std::cout << "\n[ 真实注入 ] ";
    for (int s = opts.waitSec; s > 0; --s) {
      std::cout << s << " " << std::flush;
      sleepMs(1000);
    }
    std::cout << "\n开始注入:\n";
  } else {
    std::cout << "\n[dry-run] 以下动作不会真正执行:\n";
  }

  // 1) 鼠标移动: 在起点周围画一个小方框, 演示 moveTo / moveBy
  std::cout << "[1] 鼠标移动\n";
  act(ic.get(), opts, "moveTo 起点+小偏移", [&] { return ic->moveTo(start.x + 60, start.y - 60); });
  sleepMs(150);
  act(ic.get(), opts, "moveBy 相对移动", [&] { return ic->moveBy(120, 0); });
  sleepMs(150);
  act(ic.get(), opts, "moveBy 相对移动", [&] { return ic->moveBy(0, 120); });
  sleepMs(150);
  act(ic.get(), opts, "moveBy 相对移动", [&] { return ic->moveBy(-120, 0); });
  sleepMs(150);
  act(ic.get(), opts, "moveBy 回到起点附近", [&] { return ic->moveBy(0, -120); });
  sleepMs(200);

  // 2) 点击: 在起点点击(重新确认焦点/光标位置, 让后续打字落到文本框)
  std::cout << "[2] 点击\n";
  act(ic.get(), opts, "click 起点(左键)", [&] { return ic->click(start.x, start.y); });
  sleepMs(200);

  // 3) 打字: 输入文本 + 回车
  std::cout << "[3] 打字\n";
  act(ic.get(), opts, (std::string("typeText \"") + opts.text + "\"").c_str(),
      [&] { return ic->typeText(opts.text.c_str()); });
  sleepMs(150);
  act(ic.get(), opts, "press Enter", [&] { return ic->press(KeyCode::enter); });
  sleepMs(150);
  act(ic.get(), opts, "typeText 第二行", [&] { return ic->typeText("second line by avox"); });
  sleepMs(200);

  // 4) 拖拽: 从起点向右拖一小段(在文本框里会选中文本, 可见)
  std::cout << "[4] 拖拽\n";
  act(ic.get(), opts, "drag 向右 80px", [&] { return ic->drag(start.x, start.y, start.x + 80, start.y); });
  sleepMs(200);

  // 礼貌: 光标放回起点
  act(ic.get(), opts, "moveTo 还原光标", [&] { return ic->moveTo(start.x, start.y); });

  std::cout << "\n完成。\n";
  const char* err = ic->getLastError();
  if (err && err[0]) std::cout << "提示: " << err << "\n";
  return 0;
}
