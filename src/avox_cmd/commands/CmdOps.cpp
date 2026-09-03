/**
 * @file CmdOps.cpp
 * @brief ops 子命令 - 窗口自动化基础操作 (CLI)
 *
 * 新架构: 不再经 OpsContext。截图走 IScreenCapture, 识别走 vision helpers
 * (locateText/locateImage/recognizeAll/matchAll, 拿 IImageBuffer*), 动作走 ActOps
 * (actAt/typeTextAt/pressKeyAt/hotkeyAt), 坐标转换走 IScreenCapture::toScreen。
 *
 * 扁平命令 (无多级/位置子命令): 操作由 -旗标 选定, 互斥 (选其一):
 *   ops -l [-w <子串>]              列捕获设备 (title + window/monitor; -w 空=全部)
 *   ops -f -w <窗口>                激活(前置) 子串匹配的第一个窗口
 *   ops -s -w <窗口> [-o out.png]   截图窗口 (默认存 运行目录/screenshots; -o 指定)
 *   ops -t <文字> -w <窗口> [-e 阈值] [-a 动作]   截图 + OCR 找文字 → 坐标 (→ 动作)
 *   ops -p <模板> -w <窗口> [-e 阈值] [-a 动作]   截图 + 模板匹配 → 坐标 (→ 动作)
 *   ops -T <文本> [-w <窗口>]       输入文本 (Unicode, 支持中文; -w 先激活窗口)
 *   ops -K <键名> [-w <窗口>]       按键 (enter/esc/tab/a-z 等; -w 先激活窗口)
 *   ops -H <组合> [-w <窗口>]       组合键 (逗号分隔, 如 ctrl,c; -w 先激活窗口)
 *
 * -a (click|move|dblclick|none) 合法取值由宏表驱动 (avox/AvoxInput.h)。
 */

#include "CmdOps.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "avox_cmd/CmdHelper.hpp"        // ensureDir
#include "avox/Avox.hpp"                  // getAvoxPath
#include "avox/AvoxInput.h"              // IScreenCapture/WindowEntry/InputAction/createScreenCapture/toScreen/inputActionNames/checkValidInputAction
#include "avox/Input/ShotOps.hpp"         // listWindowDevices / activeWindow (-l/-f 及纯输入激活, 无需截图上下文)
#include "avox/Input/ActOps.hpp"          // actAt / typeTextAt / pressKeyAt / hotkeyAt / parseKeyCode / parseKeyList
#include "avox/vision/OcrHelper.hpp"    // locateText / recognizeAll / formatOcrResult / LocateResult
#include "avox/vision/MatchHelper.hpp"  // locateImage / matchAll / formatMatchResult
#include "avox/AvoxVideo.h"             // createImageBuffer / loadImagePath
#include "avox/module/Time.hpp"

namespace avox {

namespace {
const char* vDeviceKindName(VDeviceKind k) {
  switch (k) {
    case VDeviceKind::camera:  return "camera";
    case VDeviceKind::window:  return "window";
    case VDeviceKind::monitor: return "monitor";
    default:                   return "none";
  }
}
// IScreenCapture RAII: create 返回裸指针, 析构 delete 释放
struct CapGuard {
  IScreenCapture* p;
  explicit CapGuard(IScreenCapture* c) : p(c) {}
  ~CapGuard() { delete p; }
  IScreenCapture* operator->() { return p; }
  explicit operator bool() const { return p != nullptr; }
};
void printOpsUsage() {
  printf("ops -<操作> -w <窗口> 或 -d <桌面> [options]   (操作互斥, 选其一; 目标: -w窗口 或 -d桌面, 默认桌面0)\n"
         "  -l [--list]    [-w <子串>]              列捕获设备 (title + window/monitor; -w 空=全部)\n"
         "  -f [--focus]   -w <窗口>                激活(前置) 第一个匹配窗口\n"
         "  -s [--shot]    [-w <窗口> | -d <桌面>]  截图 (默认桌面0, -o 指定文件)\n"
         "  -c <x,y,w,h>   [-w <窗口> | -d <桌面>]  裁剪区域并保存 (如 -c 100,200,300,400)\n"
         "  -t <文字>      [-w <窗口> | -d <桌面>] [-e 阈值] [-a ...]  找文字 (OCR)\n"
         "  -p <模板>      [-w <窗口> | -d <桌面>] [-e 阈值] [-a ...]  找图 (模板匹配)\n"
         "  -O [--ocr]     [-w <窗口> | -d <桌面>] [-e 阈值]      OCR 全量识别\n"
         "  -M [--match]   [-w <窗口> | -d <桌面>] -p <模板> [-e 阈值]  模板全量匹配\n"
         "  -T <文本>      [-w <窗口>]              输入文本 (Unicode, 支持中文)\n"
         "  -K <键名>      [-w <窗口>]              按键 (enter/esc/tab/a-z 等)\n"
         "  -H <组合>      [-w <窗口>]              组合键 (逗号分隔, 如 ctrl,c)\n"
         "  -d/--desktop: 桌面/显示器索引 (0=主屏, 默认0; 与 -w 互斥)\n"
         "  -a/--action: %s (默认 none, 仅定位不动作)\n"
         "  -e/--threshold: 阈值 [0,1] (findText 默认 0.3 / findImage 默认 0.7)\n",
         inputActionNames());
}
// 默认截图存盘路径 (运行目录/screenshots/<tag>_时间戳.png)
std::string defaultShotPath(const std::string& tag) {
  std::string dir = getAvoxPath() + "/screenshots";
  ensureDir(dir);
  return dir + "/" + tag + "_" + formatStamp_YMDHMS() + ".png";
}
}  // namespace

Command cmdOps() {
  Command cmd;
  cmd.name = "ops";
  cmd.desc = "窗口/桌面自动化: -w窗口/-d桌面指定目标, 再选操作";
  cmd.parser.addArg({"-w", "--window", ArgType::String, false,
                     "窗口标题子串 (与 -d 二选一; 用 -t/-p/-O/-M 时必须指定 -w 或 -d)", ""});
  cmd.parser.addArg({"-d", "--desktop", ArgType::Int, false,
                     "桌面/显示器索引 (0=主屏; 与 -w 二选一; 用 -t/-p/-O/-M 时必须指定 -w 或 -d)", ""});
  cmd.parser.addArg({"-l", "--list", ArgType::Boolean, false,
                     "列捕获设备 (title + window/monitor; -w 空=全部)", ""});
  cmd.parser.addArg({"-f", "--focus", ArgType::Boolean, false,
                     "激活(前置) 第一个匹配窗口", ""});
  cmd.parser.addArg({"-s", "--shot", ArgType::Boolean, false, "截图窗口", ""});
  cmd.parser.addArg({"-t", "--text", ArgType::String, false,
                     "找文字 (OCR; 需配合 -w 窗口 或 -d 桌面)", ""});
  cmd.parser.addArg({"-p", "--template", ArgType::String, false,
                     "找图模板路径 (需配合 -w 窗口 或 -d 桌面)", ""});
  cmd.parser.addArg({"-O", "--ocr", ArgType::Boolean, false,
                     "OCR 全量识别 (需配合 -w 窗口 或 -d 桌面)", ""});
  cmd.parser.addArg({"-M", "--match", ArgType::Boolean, false,
                     "模板全量匹配 (需配合 -w 窗口 或 -d 桌面, 需 -p 给模板)", ""});
  cmd.parser.addArg({"-T", "--type", ArgType::String, false,
                     "输入文本 (Unicode, 支持中文; 可选 -w 先激活窗口)", ""});
  cmd.parser.addArg({"-K", "--key", ArgType::String, false,
                     "按键名 (enter/esc/tab/a-z 等; 可选 -w 先激活窗口)", ""});
  cmd.parser.addArg({"-H", "--hotkey", ArgType::String, false,
                     "组合键, 逗号分隔 (如 ctrl,c; 可选 -w 先激活窗口)", ""});
  std::string actionDesc =
      std::string("定位后动作: ") + inputActionNames() + " (默认 none)";
  cmd.parser.addArg(
      {"-a", "--action", ArgType::String, false, actionDesc.c_str(), "none"});
  cmd.parser.addArg({"-e", "--threshold", ArgType::Number, false,
                     "阈值 [0,1] (findText 默认 0.3 / findImage 默认 0.7)", ""});
  cmd.parser.addArg({"-c", "--crop", ArgType::String, false,
                     "裁剪区域 x,y,w,h (截图后裁剪并保存)", ""});
  cmd.parser.addArg({"-o", "--output", ArgType::String, false,
                     "shot 截图存盘路径 (空=默认 运行目录/screenshots, 文件名带时间戳)",
                     ""});

  cmd.run = [](const ParsedArgs& args) -> int {
    bool wantList = args.getBool("list");
    bool wantFocus = args.getBool("focus");
    bool wantShot = args.getBool("shot");
    std::string text = args.getString("text");
    std::string tmpl = args.getString("template");
    std::string crop = args.getString("crop");
    bool wantText = !text.empty();
    bool wantImage = !tmpl.empty();
    bool wantCrop = !crop.empty();
    bool wantOcr = args.getBool("ocr");
    bool wantMatch = args.getBool("match");
    std::string typeText = args.getString("type");
    std::string keyName = args.getString("key");
    std::string hotkeyStr = args.getString("hotkey");
    bool wantType = !typeText.empty();
    bool wantKey = !keyName.empty();
    bool wantHotkey = !hotkeyStr.empty();
    int opCount = (wantList ? 1 : 0) + (wantFocus ? 1 : 0) + (wantShot ? 1 : 0) +
                  (wantText ? 1 : 0) + (wantImage ? 1 : 0) + (wantCrop ? 1 : 0) +
                  (wantOcr ? 1 : 0) + (wantMatch ? 1 : 0) +
                  (wantType ? 1 : 0) + (wantKey ? 1 : 0) + (wantHotkey ? 1 : 0);
    if (opCount == 0) {
      printf("缺少操作 (指定其一: -l/-f/-s/-t/-p/-c/-O/-M/-T/-K/-H)\n");
      printOpsUsage();
      return 1;
    }
    if (opCount > 1) {
      printf("操作互斥, 只能指定一个 (-l/-f/-s/-t/-p/-c/-O/-M/-T/-K/-H)\n");
      return 1;
    }
    std::string window = args.getString("window");
    int desktopIdx = args.getInt("desktop", 0);
    bool hasWindow = !window.empty();
    bool hasDesktop = args.has("desktop");
    if (hasWindow && hasDesktop) {
      printf("-w/--window 和 -d/--desktop 互斥, 只能指定一个\n");
      return 1;
    }
    bool useScreen = !hasWindow;
    int screenIndex = desktopIdx;
    std::string act = args.getString("action", "none");
    if (!checkValidInputAction(act.c_str())) {
      printf("--action/-a 取值仅支持 %s, 实际: %s\n", inputActionNames(),
             act.c_str());
      return 1;
    }
    InputAction action = parseInputAction(act.c_str());
    // 目标头 (窗口/桌面), 打印复用
    auto printTarget = [&]() {
      if (useScreen) printf("  screen: %d\n", screenIndex);
      else          printf("  window: %s\n", window.c_str());
    };
    // -l: 列捕获设备 (无需截图上下文, 直接 ShotOps 枚举, 支持 -w 子串过滤)
    if (wantList) {
      std::vector<WindowDeviceEntry> devs = listWindowDevices(window.c_str());
      printf("avox_cli ops -l (list)\n");
      printf("  query: %s%s\n", window.c_str(), window.empty() ? " (空=全部)" : "");
      printf("  count: %zu\n", devs.size());
      printf("  [idx] %-30s %-8s %-28s %-24s\n", "title", "kind", "class", "process");
      for (size_t i = 0; i < devs.size(); i++) {
        printf("  [%zu] %-30s %-8s %-28s %-24s\n", i, devs[i].title.c_str(),
               vDeviceKindName(devs[i].kind), devs[i].winClass.c_str(),
               devs[i].process.c_str());
      }
      return devs.empty() ? 1 : 0;
    }
    // -f: 激活(前置) — 仅窗口
    if (wantFocus) {
      if (useScreen) { printf("-f 激活只对窗口有效, 不支持桌面\n"); return 1; }
      bool ok = activeWindow(window.c_str());
      printf("avox_cli ops -f (focus)\n  window: %s\n  -> %s\n", window.c_str(),
             ok ? "OK (已置顶)" : "FAIL (找不到窗口或置顶失败)");
      return ok ? 0 : 1;
    }
    // -T/-K/-H: 纯键盘输入 (不需截图, 仅可选 -w 激活窗口)
    if (wantType || wantKey || wantHotkey) {
      if (hasWindow) {
        if (!activeWindow(window.c_str())) {
          printf("FAIL: 找不到窗口 \"%s\"\n", window.c_str());
          return 1;
        }
      }
      bool ok = false;
      const char* opTag = "";
      if (wantType) { ok = typeTextAt(typeText.c_str()); opTag = "typeText"; }
      else if (wantKey) {
        KeyCode k = parseKeyCode(keyName.c_str());
        if (k == KeyCode::none) { printf("未知键名: %s\n", keyName.c_str()); return 1; }
        ok = pressKeyAt(k); opTag = "keyPress";
      } else {
        auto keys = parseKeyList(hotkeyStr);
        if (keys.empty()) { printf("组合键为空或键名无效: %s\n", hotkeyStr.c_str()); return 1; }
        ok = hotkeyAt(keys.data(), (int32_t)keys.size()); opTag = "hotkey";
      }
      printf("avox_cli ops %s\n", wantType ? "-T (typeText)" : (wantKey ? "-K (keyPress)" : "-H (hotkey)"));
      if (hasWindow) printf("  window: %s\n", window.c_str());
      if (wantType) printf("  text: %s\n", typeText.c_str());
      if (wantKey) printf("  key: %s\n", keyName.c_str());
      if (wantHotkey) printf("  hotkey: %s\n", hotkeyStr.c_str());
      printf("  -> %s\n", ok ? "OK" : "FAIL");
      return ok ? 0 : 1;
    }
    // 以下操作都需截图: IScreenCapture 绑定目标 (窗口/桌面) 一次, 后续复用 buffer
    CapGuard cap(createScreenCapture());
    if (!cap) { printf("FAIL: createScreenCapture 失败\n"); return 1; }
    bool bound = useScreen ? cap->setScreen(screenIndex, true)
                           : cap->setWindow(window.c_str());
    if (!bound) {
      if (useScreen) printf("FAIL: 截图失败 (显示器 %d)\n", screenIndex);
      else printf("FAIL: 截图失败 (找不到窗口 \"%s\" 或截图失败)\n", window.c_str());
      return 1;
    }
    // -s: 截图
    if (wantShot) {
      printf("avox_cli ops -s (shot)\n");
      printTarget();
      printf("  target: %s\n", cap->targetName() ? cap->targetName() : "");
      printf("  size: %dx%d\n", cap->width(), cap->height());
      std::string savePath = args.getString("output");
      if (savePath.empty()) savePath = defaultShotPath("shot");
      if (cap->save(savePath.c_str())) { printf("  saved: %s\n", savePath.c_str()); return 0; }
      printf("  save 失败: %s\n", savePath.c_str());
      return 1;
    }
    // -c: 裁剪
    if (wantCrop) {
      int32_t cx = 0, cy = 0, cw = 0, ch = 0;
      if (sscanf(crop.c_str(), "%d,%d,%d,%d", &cx, &cy, &cw, &ch) != 4 || cw <= 0 || ch <= 0) {
        printf("-c/--crop 格式: x,y,w,h (如 -c 100,200,300,400)\n");
        return 1;
      }
      CapGuard sub(cap->crop(cx, cy, cw, ch));
      if (!sub || sub->width() <= 0) {
        printf("FAIL: cropImage (裁剪失败, 可能越界: x=%d y=%d w=%d h=%d)\n", cx, cy, cw, ch);
        return 1;
      }
      std::string savePath = args.getString("output");
      if (savePath.empty()) savePath = defaultShotPath("crop");
      if (sub->save(savePath.c_str())) {
        printf("avox_cli ops -c (crop)\n");
        printTarget();
        printf("  crop: x=%d y=%d w=%d h=%d\n  saved: %s\n", cx, cy, cw, ch, savePath.c_str());
        return 0;
      }
      printf("  save 失败: %s\n", savePath.c_str());
      return 1;
    }
    // -t: OCR 找文字
    if (wantText) {
      double threshold = args.has("threshold") ? (double)args.getFloat("threshold", 0.0f) : 0.3;
      LocateResult r;
      bool found = locateText(cap->getBuffer(), text.c_str(), threshold, &r);
      vec2i screen = cap->toScreen(r.imgCenter.x, r.imgCenter.y);
      printf("avox_cli ops -t (findText)\n");
      printTarget();
      printf("  text: %s\n  threshold: %.2f\n", text.c_str(), threshold);
      if (!found) { printf("  found: no\n"); return 1; }
      printf("  found: yes\n  score: %.3f\n", r.score);
      if (!r.text.empty()) printf("  matched: %s\n", r.text.c_str());
      printf("  imgCenter: (%d, %d)\n  screen: (%d, %d)\n", r.imgCenter.x, r.imgCenter.y, screen.x, screen.y);
      if (action != InputAction::none) {
        actAt(screen.x, screen.y, action);
        printf("  action: %s @ (%d, %d) -> done\n", act.c_str(), screen.x, screen.y);
      }
      return 0;
    }
    // -p: 模板匹配找图
    if (wantImage) {
      std::unique_ptr<IImageBuffer> tbuf(createImageBuffer());
      if (!loadImagePath(tmpl.c_str(), tbuf.get())) {
        printf("模板图加载失败: %s\n", tmpl.c_str());
        return 1;
      }
      double threshold = args.has("threshold") ? (double)args.getFloat("threshold", 0.0f) : 0.7;
      LocateResult r;
      bool found = locateImage(cap->getBuffer(), tbuf.get(), threshold, &r);
      vec2i screen = cap->toScreen(r.imgCenter.x, r.imgCenter.y);
      printf("avox_cli ops -p (findImage)\n");
      printTarget();
      printf("  template: %s\n  threshold: %.2f\n", tmpl.c_str(), threshold);
      if (!found) { printf("  found: no\n"); return 1; }
      printf("  found: yes\n  score: %.3f\n", r.score);
      printf("  imgCenter: (%d, %d)\n  screen: (%d, %d)\n", r.imgCenter.x, r.imgCenter.y, screen.x, screen.y);
      if (action != InputAction::none) {
        actAt(screen.x, screen.y, action);
        printf("  action: %s @ (%d, %d) -> done\n", act.c_str(), screen.x, screen.y);
      }
      return 0;
    }
    // -O: OCR 全量识别
    if (wantOcr) {
      double threshold = args.has("threshold") ? (double)args.getFloat("threshold", 0.0f) : 0.3;
      auto items = recognizeAll(cap->getBuffer(), threshold);
      printf("avox_cli ops -O (ocr)\n");
      printTarget();
      printf("  threshold: %.2f\n%s\n", threshold, formatOcrResult(items).c_str());
      return items.empty() ? 1 : 0;
    }
    // -M: 模板全量匹配
    if (wantMatch) {
      if (tmpl.empty()) { printf("模板全量匹配需要 -p/--template 给模板路径\n"); return 1; }
      std::unique_ptr<IImageBuffer> tbuf(createImageBuffer());
      if (!loadImagePath(tmpl.c_str(), tbuf.get())) { printf("模板图加载失败: %s\n", tmpl.c_str()); return 1; }
      double threshold = args.has("threshold") ? (double)args.getFloat("threshold", 0.0f) : 0.7;
      auto items = matchAll(cap->getBuffer(), tbuf.get(), threshold);
      printf("avox_cli ops -M (match)\n");
      printTarget();
      printf("  template: %s\n  threshold: %.2f\n%s\n", tmpl.c_str(), threshold, formatMatchResult(items).c_str());
      return items.empty() ? 1 : 0;
    }
    return 1;
  };
  return cmd;
}

}
