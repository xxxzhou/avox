/**
 * @file CmdVoice.cpp
 * @brief voice 子命令 - 全局语音输入 (按住热键 → 录音 → 流式识别 → 模拟键盘输入)
 *
 * 开 avox_cli voice 后台常驻, 按住全局热键 (默认 F9) 开始录音,
 * sherpa-onnx 流式识别, 识别文字实时用 SendInput KEYEVENTF_UNICODE 注入到
 * 当前焦点窗口 (游戏聊天框/记事本/终端等)。松开热键停止录音。
 *
 * 用法:
 *   avox_cli voice                      # 默认 F9 热键
 *   avox_cli voice -hotkey f9           # 用 F9 热键
 *   avox_cli voice -list-devices        # 列出麦克风设备
 *   avox_cli voice -device 1            # 指定麦克风设备索引
 *
 * 仅 Windows (依赖 SetWindowsHookEx 全局热键 + SendInput 键盘注入)。
 */

#include "CmdVoice.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "avox_cmd/CmdHelper.hpp"
#include "avox_cmd/Shell.hpp"
#include "avox_cmd/ConsoleWin32.hpp"
#include "avox/Avox.hpp"
#include "avox/AvoxAudio.h"
#include "avox/AvoxSource.h"
#include "avox/AvoxPlayer.h"
#include "avox/module/Time.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

namespace avox {

// ========== Windows Only ==========

#ifndef _WIN32
Command cmdVoice() {
  Command cmd;
  cmd.name = "voice";
  cmd.desc = "全局语音输入 (仅 Windows)";
  cmd.run = [](const ParsedArgs&) -> int {
    printf("voice 命令仅支持 Windows\n");
    return 1;
  };
  return cmd;
}
#else

namespace {

// ========== 文字注入 (Unicode 键盘事件) ==========
// 用 SendInput KEYEVENTF_UNICODE 逐字符注入, 等同于"实时打字"到前台窗口。
// 不依赖剪贴板/Ctrl+V: 游戏自绘输入框 (不响应粘贴) 也能收到文字。

// 当前 avox_cli 控制台窗口句柄 (初始化时记录, 用于排除自身)
static HWND gConsoleWnd = nullptr;

// 用 Unicode 键盘事件注入一段文字 (每个 UTF-16 码元一次 down+up)
// 增补平面字符 (emoji 等) 是代理对, 逐码元发送会被系统重组成正确字符。
void sendUnicodeText(const std::wstring& text) {
  if (text.empty()) return;
  std::vector<INPUT> ins;
  ins.reserve(text.size() * 2);
  for (wchar_t ch : text) {
    INPUT dn{}; dn.type = INPUT_KEYBOARD;
    dn.ki.wScan = ch; dn.ki.dwFlags = KEYEVENTF_UNICODE;
    INPUT up{}; up.type = INPUT_KEYBOARD;
    up.ki.wScan = ch; up.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
    ins.push_back(dn);
    ins.push_back(up);
  }
  SendInput((UINT)ins.size(), ins.data(), sizeof(INPUT));
}

// 模拟 Backspace (删除光标前一个字符)
void sendBackspace(int count) {
  for (int i = 0; i < count; ++i) {
    INPUT in[2];
    ZeroMemory(in, sizeof(in));
    in[0].type = INPUT_KEYBOARD;
    in[0].ki.wVk = VK_BACK;
    in[1].type = INPUT_KEYBOARD;
    in[1].ki.wVk = VK_BACK;
    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, in, sizeof(INPUT));
    Sleep(5);
  }
}

// 计算两个字符串的公共前缀长度 (UTF-16 码点)
int commonPrefixLen(const std::wstring& a, const std::wstring& b) {
  int len = 0;
  while (len < (int)a.size() && len < (int)b.size() && a[len] == b[len]) ++len;
  return len;
}

// UTF-8 → wstring
std::wstring toWstr(const std::string& utf8) {
  if (utf8.empty()) return {};
  int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
  if (wlen <= 1) return {};
  std::wstring w((size_t)wlen - 1, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &w[0], wlen);
  return w;
}

// ========== 增量文字注入 (Unicode 键盘事件) ==========
// 流式 partial text 是完整的 (如 "你好" → "你好世界"), 每次不是增量。
// 策略: 记录前一次输入的文字, 只注入新增部分;
// 如果 partial 被修正 (如 "你好" → "你们"), 先 Backspace 删差异再注入新后缀。
// 用 Unicode 键盘事件注入: 等同于实时打字, 兼容所有接受键盘输入的窗口 (含游戏自绘输入框)。
// 关键: 注入前检查前台窗口, 如果是 avox_cli 自己则跳过 (焦点被抢时文字不应发到控制台)。

struct TextInjector {
  std::wstring lastText;   // 已注入到目标窗口的文字 (wchar 方便算差异)

  // 检查当前前台窗口是否可用于注入 (排除 avox_cli 自身)
  bool canInject() {
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    // 如果前台是 avox_cli 控制台窗口, 说明焦点被抢了, 不注入
    if (gConsoleWnd && fg == gConsoleWnd) return false;
    return true;
  }

  // 注入新的 partial text (与上次对比, 增量输入)
  void injectPartial(const std::string& utf8) {
    std::wstring newText = toWstr(utf8);
    if (!canInject()) {
      // 目标窗口不可注入时, 同步 lastText 为当前文本;
      // 恢复注入后增量对比从当前文本开始, 不会补发之前跳过的内容
      lastText = std::move(newText);
      return;
    }
    int common = commonPrefixLen(lastText, newText);
    // 删掉尾部差异 (Backspace 删 (lastText.size() - common) 个字符)
    int delCount = (int)lastText.size() - common;
    if (delCount > 0) {
      sendBackspace(delCount);
    }
    // 注入新增后缀 (Unicode 键盘事件, 逐字符实时打字)
    if (common < (int)newText.size()) {
      sendUnicodeText(newText.substr(common));
    }
    lastText = std::move(newText);
  }

  // 注入最终结果 (可能与最后一次 partial 有差异)
  void injectFinal(const std::string& utf8) {
    injectPartial(utf8);
  }

  // 重置 (新一轮录音开始)
  void reset() { lastText.clear(); }
};

// ========== 全局热键 (WH_KEYBOARD_LL) ==========

// 热键配置: VK + 修饰键
struct HotkeyConfig {
  int vk = VK_F9;            // 默认 F9
  bool ctrl = false;
  bool shift = false;
  bool alt = false;
};

// 全局热键状态 (钩子回调 + 主循环共享)
struct HotkeyState {
  HotkeyConfig config;
  std::atomic<bool> pressed{false};
  std::atomic<bool> running{true};
  std::atomic<bool> toggleMode{false};  // 切换模式: 按一次切换录音状态 (否则按住)
  HHOOK hook = nullptr;
  // 按下/松开/切换事件通知 (主循环用 WaitForSingleObject 替代轮询)
  HANDLE pressEvent = nullptr;
  HANDLE releaseEvent = nullptr;
  HANDLE toggleEvent = nullptr;
};
static HotkeyState gHotkey;

// Ctrl+C 退出标志 + 钩子线程 ID (Ctrl+C handler 需要)
static std::atomic<bool> gVoiceQuit{false};
static DWORD gHookThreadId = 0;

// 解析热键名称 → HotkeyConfig
HotkeyConfig parseHotkey(const std::string& name) {
  HotkeyConfig cfg;
  std::string s = name;
  // 转小写方便比较
  for (auto& c : s) c = (char)tolower(c);
  // 解析修饰键 (ctrl+alt+shift+key)
  if (s.find("ctrl+") != std::string::npos) { cfg.ctrl = true; s.erase(s.find("ctrl+"), 5); }
  if (s.find("alt+") != std::string::npos) { cfg.alt = true; s.erase(s.find("alt+"), 4); }
  if (s.find("shift+") != std::string::npos) { cfg.shift = true; s.erase(s.find("shift+"), 6); }
  // 解析主键
  if (s == "right_alt" || s == "ralt") cfg.vk = VK_RMENU;
  else if (s == "left_alt" || s == "lalt") cfg.vk = VK_LMENU;
  else if (s == "right_ctrl" || s == "rctrl") cfg.vk = VK_RCONTROL;
  else if (s == "left_ctrl" || s == "lctrl") cfg.vk = VK_LCONTROL;
  else if (s.size() > 1 && s[0] == 'f') {
    // F1..F24: F13-F24 物理键盘多无, 供手柄经 Steam Input 映射, 零冲突
    int fn = 0;
    for (size_t i = 1; i < s.size(); ++i) {
      if (s[i] < '0' || s[i] > '9') { fn = 0; break; }
      fn = fn * 10 + (s[i] - '0');
    }
    if (fn >= 1 && fn <= 24) cfg.vk = VK_F1 + fn - 1;
  }
  else if (s == "space") cfg.vk = VK_SPACE;
  else if (s == "tab") cfg.vk = VK_TAB;
  else if (!s.empty() && s.size() == 1 && s[0] >= 'a' && s[0] <= 'z') cfg.vk = 'A' + (s[0] - 'a');
  return cfg;
}

// 热键名称 → 显示字符串
std::string hotkeyDisplayName(const HotkeyConfig& cfg) {
  std::string s;
  if (cfg.ctrl) s += "Ctrl+";
  if (cfg.alt) s += "Alt+";
  if (cfg.shift) s += "Shift+";
  if (cfg.vk == VK_RMENU) s += "Right Alt";
  else if (cfg.vk == VK_LMENU) s += "Left Alt";
  else if (cfg.vk == VK_RCONTROL) s += "Right Ctrl";
  else if (cfg.vk == VK_LCONTROL) s += "Left Ctrl";
  else if (cfg.vk >= VK_F1 && cfg.vk <= VK_F24) s += "F" + std::to_string(cfg.vk - VK_F1 + 1);
  else if (cfg.vk >= 'A' && cfg.vk <= 'Z') s += (char)cfg.vk;
  else s += "Key" + std::to_string(cfg.vk);
  return s;
}

// 检查当前按键是否匹配热键 (含修饰键)
bool checkHotkeyMatch(const KBDLLHOOKSTRUCT& kb, const HotkeyConfig& cfg) {
  if ((int)kb.vkCode != cfg.vk) return false;
  // KBDLLHOOKSTRUCT flags: LLKHF_ALTDOWN (bit 0x20)
  bool altDown = (kb.flags & LLKHF_ALTDOWN) != 0;
  // GetAsyncKeyState 检 Ctrl/Shift
  bool ctrlDown = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
  bool shiftDown = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
  // Alt 类键 (VK_MENU/RMENU/LMENU) 自带 LLKHF_ALTDOWN, 热键配置 alt 字段
  // 对 Alt 键本身不检查 alt 修饰 (否则 VK_RMENU 按下 altDown=true 但 cfg.alt=false
  // 导致匹配失败); 对非 Alt 键, 检查 cfg.alt 是否与 altDown 匹配
  bool isAltKey = (cfg.vk == VK_MENU || cfg.vk == VK_LMENU || cfg.vk == VK_RMENU);
  if (!isAltKey && cfg.alt != altDown) return false;
  if (cfg.ctrl != ctrlDown) return false;
  if (cfg.shift != shiftDown) return false;
  return true;
}

// WH_KEYBOARD_LL 钩子回调
// 吞掉热键的全部 keydown/keyup (含按住时的自动重复 keydown): 让系统从头到尾
// 不感知该键被按过。否则录音期间修饰键 (Alt/Ctrl/Shift) 处于按下状态, 会把
// Unicode 注入污染成 Alt+组合/菜单快捷键语义 (WM_SYSCHAR) 导致目标窗口丢字;
// 普通热键的自动重复也会混入多余字符。全吞后注入期间没有任何修饰键干扰。
LRESULT CALLBACK hotkeyHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
  if (nCode == HC_ACTION && gHotkey.running.load()) {
    KBDLLHOOKSTRUCT* kb = (KBDLLHOOKSTRUCT*)lParam;
    if (checkHotkeyMatch(*kb, gHotkey.config)) {
      if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
        // 仅首次按下触发事件 (toggle 下为切换); 每次 keydown (含自动重复) 都吞掉
        if (!gHotkey.pressed.load()) {
          gHotkey.pressed.store(true);
          if (gHotkey.toggleMode.load()) {
            if (gHotkey.toggleEvent) SetEvent(gHotkey.toggleEvent);
          } else {
            if (gHotkey.pressEvent) SetEvent(gHotkey.pressEvent);
          }
        }
        return 1;
      } else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
        gHotkey.pressed.store(false);
        // hold: 松开=停止; toggle: 松开无动作 (仅按下切换)
        if (!gHotkey.toggleMode.load()) {
          if (gHotkey.releaseEvent) SetEvent(gHotkey.releaseEvent);
        }
        return 1;
      }
    }
  }
  return CallNextHookEx(gHotkey.hook, nCode, wParam, lParam);
}

// ========== 录音+识别状态 ==========

struct VoiceState {
  std::mutex mtx;
  std::string lastPartial;     // 最近一次 partial text (用于去重)
  TextInjector injector;       // 增量文字注入器
  IAudioStt* stt = nullptr;    // sherpa 语音识别器
  IAudioSource* audioSrc = nullptr; // 音频源
  std::atomic<bool> recording{false};
  std::string accumulatedText; // 累积的最终文本 (端点检测分段)
  std::string prefix;          // 每段识别文字前缀, 注入到目标窗口 (如 "♪● ")
  bool perSentence = false;    // toggle 模式: 每句独立注入, 不累积
  // 显示解耦: 识别线程 (STT 回调) 与录音线程 (start/stopRecording) 都不直接 printf —— 控制
  // 台输出在鼠标点击/框选 conhost 时会阻塞, 一旦这些关键线程卡在 printf 上, 识别就停摆且
  // stt->stop() (join 识别线程) 永远完不成 → 必须 Ctrl+C。改为 push 到此队列, 由主线程统一
  // 打印; 关键线程只做内存操作, 永不因控制台阻塞而卡死。
  std::mutex dispMtx;
  std::deque<std::string> dispQueue;
  void disp(const std::string& line) {
    std::lock_guard<std::mutex> lk(dispMtx);
    // in-place 更新 (行尾无 \n, 如 "🎤 partial"): 替换队尾上一个 in-place 行, 避免主线程
    // 短暂阻塞期间堆积一堆互相覆盖的 partial。带 \n 的行 (最终结果) 正常追加。
    bool inPlace = !line.empty() && line.back() != '\n';
    if (inPlace && !dispQueue.empty() && !dispQueue.back().empty() &&
        dispQueue.back().back() != '\n') {
      dispQueue.back() = line;
    } else {
      dispQueue.push_back(line);
    }
  }
};

// STT 观察者: 接收流式识别结果
class VoiceSttOb : public IAudioSttOb {
 public:
  VoiceSttOb(VoiceState& s) : state(s) {}
  void onResult(const SttResult& result, const char* text) override {
    if (!text || !text[0]) return;
    std::lock_guard<std::mutex> lk(state.mtx);
    if (state.perSentence) {
      // 逐句模式: 注入最终结果后清空, 下一句从零开始
      std::string full = state.prefix + text;
      state.injector.injectFinal(full);
      state.disp(std::string("\r\033[K  ✓ ") + full + "\n");
      state.injector.reset();
      state.lastPartial.clear();
    } else {
      // 累积模式: 端点分段间加空格
      if (!state.accumulatedText.empty()) {
        state.accumulatedText += ' ';
      }
      state.accumulatedText += text;
      state.lastPartial.clear();
    }
  }
  void onPartialResult(const char* text) override {
    if (!text || !text[0]) return;
    std::lock_guard<std::mutex> lk(state.mtx);
    std::string full;
    if (state.perSentence) {
      // 逐句模式: 只注入当前句 (prefix + partial), 不累积
      full = state.prefix + text;
    } else {
      // 累积模式: prefix + 已确认文本 + 当前 partial
      full = state.prefix + state.accumulatedText + text;
    }
    if (full == state.lastPartial) return;   // 无变化
    state.lastPartial = full;
    // 增量注入到目标窗口
    state.injector.injectPartial(full);
    state.disp(std::string("\r\033[K  🎤 ") + full);
  }
  void onEndpoint() override {
    // 端点检测: 一段话结束, 结果由 onResult 回调
  }
 private:
  VoiceState& state;
};

// AudioTap 观察者: 从 IAudioRender 获取音频帧, 喂给 STT
// 必须用虚类实现 (IAudioTapOb 不支持 lambda), 通过 addAudioTapOb 注册
class VoiceTapOb : public IAudioTapOb {
 public:
  VoiceTapOb(VoiceState& s, IAudioStt* stt) : state(s), stt(stt) {}
  void onAudioDesc(const AudioDesc& desc) override {
    stt->setAudioDesc(desc);
    descSet = true;
  }
  void onFrame(const AvoxData& raw, int64_t pts) override {
    if (descSet && state.recording.load()) {
      stt->recognize(raw, pts);
    }
  }
  bool descSet = false;
 private:
  VoiceState& state;
  IAudioStt* stt;
};

// ========== 列出音频设备 ==========

void listAudioDevices() {
  ADeviceSdk asdk = getDefaltAudioSdk();
  IAudioManager* audioMgr = getAudioManager(asdk);
  if (!audioMgr) {
    printf("无法获取音频设备管理器\n");
    return;
  }
  int count = audioMgr->getDeviceCount();
  printf("音频设备 (%d):\n", count);
  for (int i = 0; i < count; ++i) {
    IAudioSource* dev = audioMgr->getDevice(i);
    if (!dev) continue;
    printf("  %d. %s\n", i, dev->getDeviceName());
  }
}

// Ctrl+C 处理: 置退出标志 + 唤醒所有等待点 + 唤醒钩子线程
BOOL WINAPI voiceCtrlHandler(DWORD ctrl) {
  if (ctrl == CTRL_C_EVENT || ctrl == CTRL_BREAK_EVENT) {
    gVoiceQuit.store(true);
    gHotkey.running.store(false);
    // 唤醒主循环的所有 WaitForSingleObject (pressEvent/releaseEvent/toggleEvent)
    if (gHotkey.pressEvent) SetEvent(gHotkey.pressEvent);
    if (gHotkey.releaseEvent) SetEvent(gHotkey.releaseEvent);
    if (gHotkey.toggleEvent) SetEvent(gHotkey.toggleEvent);
    // 唤醒钩子线程 (投递 WM_QUIT)
    if (gHookThreadId) PostThreadMessageA(gHookThreadId, WM_QUIT, 0, 0);
    return TRUE;
  }
  return FALSE;
}

}  // namespace

Command cmdVoice() {
  Command cmd;
  cmd.name = "voice";
  cmd.desc = "全局语音输入: 热键录音→流式识别→模拟键盘输入 (hold 按住 / toggle 切换)";
  cmd.parser.addArg({"-mode", "", ArgType::String, false,
                     "触发模式: toggle 按一次切换 (默认) / hold 按住说话", "toggle"});
  cmd.parser.addArg({"-hotkey", "", ArgType::String, false,
                     "全局热键 (right_alt/ctrl+f9/f24 等); 不填按 mode 取默认", ""});
  cmd.parser.addArg({"-device", "", ArgType::Int, false,
                     "麦克风设备索引 (默认 0)", "0"});
  cmd.parser.addArg({"-prefix", "", ArgType::String, false,
                     "每段识别文字前缀, 注入到目标窗口; 传空串关闭", "♪● "});
  cmd.parser.addArg({"-list-devices", "", ArgType::Boolean, false,
                     "列出麦克风设备后退出", ""});

  cmd.run = [](const ParsedArgs& args) -> int {
    if (args.getBool("list-devices")) {
      listAudioDevices();
      return 0;
    }

    // 默认参数优先级: CLI 参数 > cli.json voice 节点 > 内置默认
    Json cliCfg = loadCliConfig();
    std::string cfgMode = "toggle";
    std::string cfgPrefix = "♪● ";
    std::string cfgHotkeyToggle = "f9";
    std::string cfgHotkeyHold = "f9";
    if (cliCfg.bObject() && cliCfg.find("voice") && cliCfg["voice"].bObject()) {
      const Json& voiceCfg = cliCfg["voice"];
      if (voiceCfg.find("mode") && voiceCfg["mode"].bString()) cfgMode = voiceCfg["mode"].get<std::string>();
      if (voiceCfg.find("prefix") && voiceCfg["prefix"].bString()) cfgPrefix = voiceCfg["prefix"].get<std::string>();
      if (voiceCfg.find("hotkeyToggle") && voiceCfg["hotkeyToggle"].bString()) cfgHotkeyToggle = voiceCfg["hotkeyToggle"].get<std::string>();
      if (voiceCfg.find("hotkeyHold") && voiceCfg["hotkeyHold"].bString()) cfgHotkeyHold = voiceCfg["hotkeyHold"].get<std::string>();
    }
    std::string mode = !args.getStringList("mode").empty() ? args.getString("mode") : cfgMode;
    bool toggleMode = (mode == "toggle");
    // 热键: CLI > 当前模式配置热键 (hotkeyToggle/hotkeyHold) > f9
    std::string hotkeyName = !args.getStringList("hotkey").empty() ? args.getString("hotkey") : (toggleMode ? cfgHotkeyToggle : cfgHotkeyHold);
    int deviceIdx = args.getInt("device", 0);
    std::string prefix = !args.getStringList("prefix").empty() ? args.getString("prefix") : cfgPrefix;

    // 解析热键
    HotkeyConfig hotkeyCfg = parseHotkey(hotkeyName);
    std::string hotkeyDisp = hotkeyDisplayName(hotkeyCfg);

    // 启用 VT (ANSI 颜色/光标)
    cmdEnableVt();

    // 记录控制台窗口句柄 (注入文字时排除自身, 避免发到 avox_cli)
    gConsoleWnd = conGetConsoleWindow();

    // 关掉控制台 QuickEdit 选区 + 鼠标事件 (cmdQuietInputGuard RAII, 出作用域自动恢复):
    // conhost 在鼠标点击/框选时会阻塞本进程的控制台输出写; voice 把识别结果解耦到主线程
    // 显示队列 (见下方 disp) 已是主防线, 这里再关鼠标让点击终端尽量不干扰输出。
    cmdQuietInputGuard quietGuard;

    // 创建 STT 识别器
    IAudioStt* stt = createAudioStt(AudioSttType::sherpa);
    if (!stt) {
      fprintf(stderr, "创建语音识别器失败 (sherpa 插件未注册?)\n");
      fprintf(stderr, "确保 avox_sherpa 插件存在且模型已下载\n");
      return 1;
    }
    stt->setRecognizerType(RecognizerType::streaming);
    stt->setModelLevel(ModelLevel::base);

    VoiceState voiceState;
    voiceState.stt = stt;
    voiceState.prefix = prefix;
    voiceState.perSentence = toggleMode;
    VoiceSttOb sttOb(voiceState);
    addAudioSttOb(stt, &sttOb);

    // 加载模型
    printf("加载语音识别模型...\n");
    stt->start();
    // 等待模型就绪 (最多 60s)
    auto t0 = std::chrono::steady_clock::now();
    while (!stt->loading() && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(60)) {
      Sleep(300);
    }
    if (!stt->loading()) {
      fprintf(stderr, "模型加载超时 (用 fetch_assets 下载模型)\n");
      removeAudioSttOb(stt, &sttOb);
      return 1;
    }
    printf("模型就绪\n");

    // 获取音频设备
    ADeviceSdk asdk = getDefaltAudioSdk();
    IAudioManager* audioMgr = getAudioManager(asdk);
    IAudioSource* audioSrc = nullptr;
    if (audioMgr && deviceIdx >= 0 && deviceIdx < audioMgr->getDeviceCount()) {
      audioSrc = audioMgr->getDevice(deviceIdx);
    }
    if (!audioSrc) {
      fprintf(stderr, "麦克风设备不可用 (idx=%d)\n", deviceIdx);
      removeAudioSttOb(stt, &sttOb);
      return 1;
    }
    voiceState.audioSrc = audioSrc;
    printf("麦克风: %s\n", audioSrc->getDeviceName());

    // 注册全局热键
    gHotkey.config = hotkeyCfg;
    gHotkey.pressed.store(false);
    gHotkey.running.store(true);
    gHotkey.toggleMode.store(toggleMode);
    gHotkey.pressEvent = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    gHotkey.releaseEvent = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    gHotkey.toggleEvent = CreateEventA(nullptr, FALSE, FALSE, nullptr);

    // 钩子线程 (WH_KEYBOARD_LL 需要消息循环)
    std::thread hookThread([]() {
      gHotkey.hook = SetWindowsHookExA(WH_KEYBOARD_LL, hotkeyHookProc,
                                       GetModuleHandleA(nullptr), 0);
      if (!gHotkey.hook) {
        fprintf(stderr, "注册全局热键失败 (需管理员权限?)\n");
        gHotkey.running.store(false);
        return;
      }
      MSG msg;
      while (gHotkey.running.load() && GetMessageA(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
      }
      UnhookWindowsHookEx(gHotkey.hook);
      gHotkey.hook = nullptr;
    });
    // 保存钩子线程 ID (Ctrl+C handler 需要投递 WM_QUIT)
    gHookThreadId = GetThreadId(hookThread.native_handle());

    printf("\n=== 全局语音输入 (%s 模式) ===\n", toggleMode ? "切换" : "按住");
    if (toggleMode) {
      printf("按一次 %s 开始录音, 再按一次停止\n", hotkeyDisp.c_str());
    } else {
      printf("按住 %s 开始录音, 松开停止\n", hotkeyDisp.c_str());
    }
    printf("前缀: %s\n", prefix.empty() ? "(无)" : prefix.c_str());
    printf("识别文字将实时输入到当前焦点窗口\n");
    printf("(可自定义: -hotkey <键> / -mode hold|toggle / -prefix <前缀>)\n");
    printf("Ctrl+C 退出\n\n");

    // Ctrl+C 处理
    gVoiceQuit.store(false);
    SetConsoleCtrlHandler(voiceCtrlHandler, TRUE);

    // 录音+识别工作线程: 按热键开始/停止录音, 流式识别 + 注入。
    // hold: 按住→录, 松开→停; toggle: 按一次→切换录音状态。
    // 不在主线程阻塞, 让钩子消息循环和 Ctrl+C handler 正常工作。
    std::thread voiceThread([&]() {
      // 当前录音会话的采集对象 (start 创建, stop 释放)
      ISourcePlayer* player = nullptr;
      IAudioRender* ar = nullptr;
      VoiceTapOb* tapOb = nullptr;

      // 销毁当前轮采集对象并释放 (createDevicePlayer 返回的是需调用方 delete 的指针,
      // 之前只 player=nullptr 不 delete → 每轮泄漏一个 player 及其 RunTask 线程):
      // 先关 AudioTap (内部 join tap 线程并排干尾部帧, recording 仍 true 时喂给 STT),
      // 再 delete tapOb, 最后 close 并 delete player (其析构 join RunTask, 释放内部
      // AudioOutput/DeviceSource)。ar 由 player 持有, delete 后即悬空, 故置空。
      auto destroySession = [&]() {
        if (ar) { ar->closeTap(); removeAudioTapOb(ar, tapOb); }
        if (tapOb) { delete tapOb; tapOb = nullptr; }
        if (player) { player->close(); delete player; player = nullptr; }
        ar = nullptr;
      };

      // 开始一轮录音: 启 STT + 创建采集 + 开 AudioTap; 失败回滚返回 false
      auto startRecording = [&]() -> bool {
        // 先清空状态, 再 start: 避免 stt->start() 回调 onPartialResult 时读到旧数据
        voiceState.injector.reset();
        voiceState.lastPartial.clear();
        voiceState.accumulatedText.clear();
        stt->start();
        voiceState.recording.store(true);
        // 纯音频采集: 不设视频源, 不需要 ISurfaceRender
        player = createDevicePlayer();
        player->setAudioSource(audioSrc);
        ar = player->getAudioRender();
        tapOb = new VoiceTapOb(voiceState, stt);
        if (ar) {
          addAudioTapOb(ar, tapOb);
          // openTap(AudioDesc{} = 跟随源格式不重采样, 100ms 一帧)
          ar->openTap(AudioDesc{}, 100);
        }
        if (!player->open()) {
          // 打开失败: 回滚 (destroySession 会 delete player, 不泄漏), recording 复位 false。
          // toggle 模式靠 recording.load() 驱动状态机, 失败后下次热键自动重试。
          voiceState.disp("\r\033[KSourcePlayer open 失败 (设备被占用?)\n");
          destroySession();
          voiceState.recording.store(false);
          return false;
        }
        // 成功后再提示 (避免失败时还显示"录音中"误导)
        voiceState.disp("\r\033[K🎤 录音中...\n");
        return true;
      };

      // 停止一轮录音: 关采集 + stop 排空 flush 最终结果 + 注入
      auto stopRecording = [&]() {
        // recording 延后到 destroySession(closeTap) 之后置 false: closeTap 内 AudioTap 会
        // 排干队列尾部帧, 经 VoiceTapOb::onFrame(gate recording) 喂给 STT; 若先置 false,
        // drain 的尾部帧会被 gate 丢弃 → 第二轮尾部不完整。closeTap 返回即 tap 线程已 join,
        // 其后翻转 recording / 关 player 都不会再触发 onFrame。
        destroySession();
        voiceState.recording.store(false);
        // stop: stopTask 阻塞 join, RunTask 排空剩余帧 + flush 最终结果 + reset stream
        stt->stop();
        std::lock_guard<std::mutex> lk(voiceState.mtx);
        if (voiceState.perSentence) {
          // 逐句模式: onResult 已逐句注入并清空, stop 后无需再注入
          if (voiceState.lastPartial.empty() && voiceState.accumulatedText.empty()) {
            voiceState.disp("\r\033[K  (无识别结果)\n");
          }
        } else {
          std::string full = voiceState.prefix + voiceState.accumulatedText;
          if (!voiceState.accumulatedText.empty()) {
            voiceState.injector.injectFinal(full);
            voiceState.disp(std::string("\r\033[K  ✓ ") + full + "\n");
          } else {
            voiceState.disp("\r\033[K  (无识别结果)\n");
          }
        }
      };

      if (gHotkey.toggleMode.load()) {
        // toggle 模式: 启动后自动开始录音, 每次热键按下翻转录音状态
        startRecording();
        while (!gVoiceQuit.load()) {
          DWORD wait = WaitForSingleObject(gHotkey.toggleEvent, 200);
          if (wait != WAIT_OBJECT_0) continue;
          if (gVoiceQuit.load()) break;
          if (voiceState.recording.load()) {
            stopRecording();
          } else {
            startRecording();
          }
        }
        // 退出时若仍在录音, 收尾
        if (voiceState.recording.load()) stopRecording();
      } else {
        // hold 模式: 按下→开始, 松开→停止
        while (!gVoiceQuit.load()) {
          DWORD wait = WaitForSingleObject(gHotkey.pressEvent, 200);
          if (wait != WAIT_OBJECT_0) continue;
          if (gVoiceQuit.load()) break;
          if (!startRecording()) continue;
          // 等待热键松开 (或退出)
          while (!gVoiceQuit.load()) {
            DWORD w = WaitForSingleObject(gHotkey.releaseEvent, 100);
            if (w == WAIT_OBJECT_0) break;
          }
          stopRecording();
        }
      }
    });

    // 主线程: 排空显示队列 + 等退出信号。
    // 所有控制台输出集中在此打印 (识别/录音线程只 push, 不直接 printf), 这样即便鼠标
    // 点击/框选 conhost 阻塞了控制台输出, 也只可能让本显示线程短暂卡住, 识别与录音循环
    // (在另外的线程) 完全不受影响, 不再出现"点一下终端就识别停摆、要 Ctrl+C"的问题。
    while (!gVoiceQuit.load()) {
      std::deque<std::string> lines;
      {
        std::lock_guard<std::mutex> lk(voiceState.dispMtx);
        lines.swap(voiceState.dispQueue);
      }
      for (const auto& l : lines) {
        fputs(l.c_str(), stdout);
      }
      if (!lines.empty()) fflush(stdout);
      Sleep(20);
    }

    // 清理: 等工作线程结束
    if (voiceThread.joinable()) voiceThread.join();

    // 排空残留显示 (退出前最后几条结果)
    {
      std::deque<std::string> tail;
      std::lock_guard<std::mutex> lk(voiceState.dispMtx);
      tail.swap(voiceState.dispQueue);
      for (const auto& l : tail) fputs(l.c_str(), stdout);
      if (!tail.empty()) fflush(stdout);
    }

    // 清理
    gHotkey.running.store(false);
    if (gHookThreadId) PostThreadMessageA(gHookThreadId, WM_QUIT, 0, 0);
    if (hookThread.joinable()) hookThread.join();
    if (gHotkey.pressEvent) CloseHandle(gHotkey.pressEvent);
    if (gHotkey.releaseEvent) CloseHandle(gHotkey.releaseEvent);
    if (gHotkey.toggleEvent) CloseHandle(gHotkey.toggleEvent);
    removeAudioSttOb(stt, &sttOb);
    stt->stop();

    // quietGuard 析构自动恢复控制台输入模式 (重新允许 QuickEdit 鼠标选区)

    printf("Bye.\n");
    return 0;
  };

  return cmd;
}

#endif  // _WIN32

}
