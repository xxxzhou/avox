#include "avox_cmd/Shell.hpp"
#include "avox_cmd/CmdHelper.hpp"
#include "avox_cmd/CmdRegistry.hpp"
#include "avox/Avox.hpp"
#include "avox/AvoxLog.h"
#include "avox/AvoxVersion.h"
#include "avox/module/Json.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <csignal>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace avox {

// tokenizeLine

std::vector<std::string> tokenizeLine(const std::string& line) {
  std::vector<std::string> tokens;
  size_t i = 0;
  while (i < line.size()) {
    // 跳过空白
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    if (i >= line.size()) break;
    if (line[i] == '"') {
      // 双引号包裹的 token
      ++i;  // 跳过左引号
      std::string token;
      while (i < line.size() && line[i] != '"') {
        if (line[i] == '\\' && i + 1 < line.size() && line[i + 1] == '"') {
          token += '"';
          i += 2;
        } else {
          token += line[i];
          ++i;
        }
      }
      if (i < line.size()) ++i;  // 跳过右引号
      tokens.push_back(std::move(token));
    } else {
      // 非引号 token
      std::string token;
      while (i < line.size() && line[i] != ' ' && line[i] != '\t') {
        token += line[i];
        ++i;
      }
      tokens.push_back(std::move(token));
    }
  }
  return tokens;
}

// 快速命令: 编号 -> 预设命令 (base 末尾通常 -i, 续行预填整条命令可编辑后补链接执行)
// 编号(num)按位置自动生成 (1,2,3...); base/desc 来自 assets/config/cli.json,
// 首次运行(配置缺失/空)写入默认值, 之后读配置, 改完重启 cli 生效。
struct QuickCmd {
  std::string num;   // 位置自动: "1","2",...
  std::string base;  // 预设命令, 空格分隔, 如 "play -io auto -i"
  std::string desc;
};
// 默认快捷命令 (仅 base + desc; num 由位置生成)
struct QuickCmdDefault {
  const char* base;
  const char* desc;
};
static const QuickCmdDefault kQuickDefaults[] = {
    {"play -io auto -i", "播放 (本地ffmpeg/网络zlmediakit 自动)"},
    {"play -io auto -log-packet -i", "播放 (同1, 额外打印IO包日志)"},
    {"play -io ffmpeg -log-packet -i", "播放 (固定ffmpeg, 额外打印IO包日志)"},
    {"play -io auto -log-packet -log-decode -log-render -i",
     "播放 (同2, 加解码/渲染日志)"},
    {"record -io ffmpeg -i", "录制 (转封装, 不转码)"},
    {"record -io ffmpeg -tc -i", "录制 (转码录制, 解码→编码)"},
    {"vision -s D://1.jpg", "OCR识别图片"},
};
constexpr size_t kQuickDefaultCount =
    sizeof(kQuickDefaults) / sizeof(kQuickDefaults[0]);

// 配置文件路径: <运行目录>/assets/config/cli.json
static std::string cliConfigPath() {
  return getAvoxPath() + "/assets/config/cli.json";
}

// 递归创建路径所在目录 (跨平台)
static void ensureFileDir(const std::string& path) {
  size_t pos = path.find_last_of("/\\");
  if (pos == std::string::npos) return;
  std::string dir = path.substr(0, pos);
#ifdef _WIN32
  size_t idx = 0;
  // 跳过驱动器号, 如 "C:\"
  if (dir.size() > 2 && dir[1] == ':') {
    idx = 2;
    if (idx < dir.size() && (dir[idx] == '\\' || dir[idx] == '/')) ++idx;
  }
  while ((idx = dir.find_first_of("/\\", idx + 1)) != std::string::npos) {
    CreateDirectoryA(dir.substr(0, idx).c_str(), nullptr);
  }
  CreateDirectoryA(dir.c_str(), nullptr);
#else
  // 逐级创建目录(iOS 禁用 system(), 用 POSIX mkdir)
  size_t idx = 0;
  while ((idx = dir.find_first_of('/', idx + 1)) != std::string::npos) {
    mkdir(dir.substr(0, idx).c_str(), 0755);
  }
  mkdir(dir.c_str(), 0755);
#endif
}

// voice 默认参数 (cli.json 缺失时写入; 与 CmdVoice 内置兜底一致)
static const char* kCliVoiceDefaultMode = "toggle";
static const char* kCliVoiceDefaultPrefix = "♪● ";
static const char* kCliVoiceDefaultHotkeyToggle = "f9";
static const char* kCliVoiceDefaultHotkeyHold = "f9";
// 默认 cli 配置序列化为 JSON (带缩进, 方便用户编辑)
// 结构: { "cmds":[{cmd,desc}...], "voice":{mode,prefix,hotkeyToggle,hotkeyHold} }
static std::string dumpCliDefaults() {
  Json root;  // null -> operator[] 转对象
  Json cmds;  // null -> push_back 转数组
  for (size_t i = 0; i < kQuickDefaultCount; ++i) {
    Json item;  // null -> operator[] 转对象
    item["cmd"] = kQuickDefaults[i].base;
    item["desc"] = kQuickDefaults[i].desc;
    cmds.push_back(item);
  }
  root["cmds"] = cmds;
  Json voice;  // voice 节点: 语音输入默认参数
  voice["mode"] = kCliVoiceDefaultMode;
  voice["prefix"] = kCliVoiceDefaultPrefix;
  voice["hotkeyToggle"] = kCliVoiceDefaultHotkeyToggle;
  voice["hotkeyHold"] = kCliVoiceDefaultHotkeyHold;
  root["voice"] = voice;
  return root.dump(2);
}

// 由默认表构造 (带编号)
static std::vector<QuickCmd> buildQuickFromDefaults() {
  std::vector<QuickCmd> out;
  out.reserve(kQuickDefaultCount);
  for (size_t i = 0; i < kQuickDefaultCount; ++i) {
    QuickCmd q;
    q.num = std::to_string(i + 1);
    q.base = kQuickDefaults[i].base;
    q.desc = kQuickDefaults[i].desc;
    out.push_back(std::move(q));
  }
  return out;
}

// 加载 cli 配置 (assets/config/cli.json):
// 缺失/空 -> 写默认值 (cmds+voice) 并返回; 解析失败 -> 返回内存默认 (不覆盖文件)。
// 返回对象结构: { "cmds":[{cmd,desc}...], "voice":{mode,prefix} }
// Shell 快捷菜单 (cmds) 与 voice 子命令 (voice) 共用同一份。
Json loadCliConfig() {
  std::string path = cliConfigPath();
  std::ifstream f(path);
  std::string content((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());
  f.close();
  if (content.empty()) {
    // 配置缺失/空: 写默认值, 返回默认
    std::string dumped = dumpCliDefaults();
    ensureFileDir(path);
    std::ofstream of(path, std::ios::binary);
    if (of.is_open()) {
      of.write(dumped.data(), (std::streamsize)dumped.size());
    }
    return parserJson(dumped.c_str());
  }
  Json root = parserJson(content.c_str());
  if (!root.bObject()) return parserJson(dumpCliDefaults().c_str());  // 解析失败: 内存默认
  return root;
}

// 加载快捷命令 (cli.json 的 cmds 节点): 配置缺失/空/无 cmds -> 用默认
static std::vector<QuickCmd> loadQuickCmds() {
  Json root = loadCliConfig();
  if (!root.bObject() || !(root.find("cmds") && root["cmds"].bArray())) {
    return buildQuickFromDefaults();
  }
  const Json& cmds = root["cmds"];
  std::vector<QuickCmd> out;
  for (size_t i = 0; i < cmds.size(); ++i) {
    const Json& item = cmds[i];
    if (!item.bObject()) continue;
    std::string cmd;
    if (item.find("cmd") && item["cmd"].bString()) {
      cmd = item["cmd"].get<std::string>();
    }
    if (cmd.empty()) continue;
    std::string desc;
    if (item.find("desc") && item["desc"].bString()) {
      desc = item["desc"].get<std::string>();
    }
    QuickCmd q;
    q.num = std::to_string(out.size() + 1);
    q.base = cmd;
    q.desc = desc;
    out.push_back(std::move(q));
  }
  if (out.empty()) return buildQuickFromDefaults();
  return out;
}

// 打印快速命令菜单
static void printQuickMenu(const std::vector<QuickCmd>& cmds) {
  printf("快速命令 (输入编号回车, 续行预填命令可整行编辑: 删/改参数、补链接, 回车执行;\n");
  printf("  预设可在 assets/config/cli.json 配置):\n");
  for (const auto& q : cmds) {
    printf("  %s  %-26s %s\n", q.num.c_str(), q.base.c_str(), q.desc.c_str());
  }
  printf("也可一行直输: 1 rtsp://192.168.1.100/live\n");
}

// 展开快速命令为最终 tokens。
// rest 非空 (一行直输 1 <链接>) -> base + rest;
// rest 为空 (只输编号):
//   base 自包含 (末尾无待补值的 -i) -> 直接执行;
//   base 需补值 -> 预设命令预填进输入行 (raw 行编辑器), 用户可整行编辑 (删参数/改参数)
//   后再补链接, 回车执行; 清空行回车 = 取消; raw 不可用 (管道/重定向) 退回打印续行只读
//   链接的老路径; EOF 返回空。
static std::vector<std::string> resolveQuick(
    const QuickCmd& q, const std::vector<std::string>& rest) {
  std::vector<std::string> base = tokenizeLine(q.base);
  if (!rest.empty()) {
    for (const auto& t : rest) base.push_back(t);
    return base;
  }
  // 自包含: 末尾不是待补值的 flag (如 -i), 直接执行
  if (!base.empty() && base.back() != "-i") {
    return base;
  }
  // 需补值: 预设命令可编辑续行
  std::string cont;
  cmdRawGuard rawGuard;
  if (rawGuard.entered()) {
    if (!cmdReadLineRaw(cont, nullptr, "avox> ", q.base + " ")) {
      printf("\n");
      return {};  // EOF
    }
  } else {
    printf("avox> %s ", q.base.c_str());
    fflush(stdout);
    if (!cmdReadLine(cont)) {
      printf("\n");
      return {};  // EOF
    }
  }
  while (!cont.empty() && (cont.back() == '\n' || cont.back() == '\r' ||
                           cont.back() == ' ' || cont.back() == '\t')) {
    cont.pop_back();
  }
  printf("\n");
  auto contTokens = tokenizeLine(cont);
  if (contTokens.empty()) {
    printf("已取消 (输入行为空)\n");
    return {};
  }
  for (const auto& t : contTokens) base.push_back(t);
  return base;
}

// Windows: Ctrl+C 处理器 (输入阶段吞掉信号)
#ifdef _WIN32
static BOOL WINAPI shellCtrlHandler(DWORD ctrlType) {
  if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_BREAK_EVENT) {
    // 吞掉 Ctrl+C, 不终止进程
    return TRUE;
  }
  return FALSE;
}
#endif

// 安装 Shell 的 Ctrl+C 处理器
static void installShellCtrlHandler() {
#ifdef _WIN32
  SetConsoleCtrlHandler(shellCtrlHandler, TRUE);
#else
  std::signal(SIGINT, SIG_IGN);
#endif
}

// 移除 Shell 的 Ctrl+C 处理器 (让命令自行处理信号)
static void removeShellCtrlHandler() {
#ifdef _WIN32
  SetConsoleCtrlHandler(shellCtrlHandler, FALSE);
#else
  std::signal(SIGINT, SIG_DFL);
#endif
}

// Shell::run

int Shell::run(CmdRegistry& registry) {
  // 非交互式 stdin 不进入 shell
  if (!cmdIsInteractive()) {
    return 1;
  }
  // VT 地基 (Win: UTF-8 CP + ANSI 转义处理; POSIX: no-op) — 颜色/光标依赖此
  cmdEnableVt();
  // 排空异步日志队列, 等初始化日志 (Load codec 等) 全部输出到控制台
  flushLog();
  // 静默后续 avox 日志 (交互输入阶段不需要日志输出到控制台)
  SilentLogOb silentOb;
  setLogObserver(&silentOb);
  // 清屏: 把初始化日志刷掉, 用户看到的是干净的帮助和提示符
  cmdClearScreen();
  // 打印帮助和交互提示
  printf("%s", registry.helpText().c_str());
  printf("\nInteractive mode. Type 'help' for commands, 'quit' to exit.\n");
  printf("play/record 的运行日志写入 logs/play_*.log / logs/record_*.log\n\n");
  // 加载快捷命令 (首次缺失/空会写默认值到 assets/config/cli.json)
  auto quickCmds = loadQuickCmds();
  printQuickMenu(quickCmds);
  printf("\n");
  // 安装 Ctrl+C 处理器 (输入阶段)
  installShellCtrlHandler();
  while (true) {
    printf("avox> ");
    fflush(stdout);
    // 读取一行
    std::string buf;
    if (!cmdReadLine(buf)) {
      break;  // EOF
    }
    // trim 尾部空白
    while (!buf.empty() && (buf.back() == '\n' || buf.back() == '\r' ||
                            buf.back() == ' ' || buf.back() == '\t')) {
      buf.pop_back();
    }
    // 分词
    auto orig = tokenizeLine(buf);
    if (orig.empty()) continue;
    // 快速命令编号: 只输编号 -> 两步 (展开预设再读链接); 一行带链接 -> 直输
    std::vector<std::string> tokens;
    const QuickCmd* matched = nullptr;
    for (const auto& q : quickCmds) {
      if (orig[0] == q.num) {
        matched = &q;
        break;
      }
    }
    if (matched) {
      std::vector<std::string> rest(orig.begin() + 1, orig.end());
      tokens = resolveQuick(*matched, rest);
      if (tokens.empty()) continue;  // 取消或 EOF
    } else {
      tokens = std::move(orig);
    }
    // -i 的语义归各子命令自己的 ArgParser 定义 (play/record=输入源URL, assets=交互开关),
    // Shell 不在此越权拦截; 行尾落空 -i / 缺输入由各命令自行报错。
    // (快速命令的"两步补链接"UX 在 resolveQuick 内独立完成, 不依赖此处。)
    // Shell 内建命令
    const std::string& cmd = tokens[0];
    if (cmd == "quit" || cmd == "exit") break;
    if (cmd == "help" || cmd == "-help" || cmd == "--help") {
      printf("%s", registry.helpText().c_str());
      continue;
    }
    if (cmd == "-version" || cmd == "--version") {
      printf("avox_cli %s\n", AVOX_COMMIT_VERSION);
      continue;
    }
    // 构建 argc/argv
    int cmdArgc = static_cast<int>(tokens.size());
    std::vector<const char*> cmdArgv;
    cmdArgv.reserve(cmdArgc);
    for (const auto& t : tokens) {
      cmdArgv.push_back(t.c_str());
    }
    // 移除 Shell Ctrl+C 处理器, 让命令自行处理信号
    removeShellCtrlHandler();
    // avox 日志默认静默不打 shell (交互阶段 observer 已是 silentOb)。
    // 仅 cmdPlay/cmdRecord 需要日志: 二者内部各自创建 FileLogOb (logs/play_*.log /
    // logs/record_*.log) 并 setLogObserver 接管, 结束时 restoreLogObserver 恢复回
    // silentOb; 其余命令 (device/input/ops/vision) 不产生独立日志文件。
    // 执行子命令
    int rc = registry.execute(cmdArgc, cmdArgv.data());
    // 重新安装 Shell Ctrl+C 处理器
    installShellCtrlHandler();
    // 排空异步日志队列: cmdPlay/cmdRecord 收尾残留此时已写回各自日志文件, 不会打到 shell
    flushLog();
    // 等命令异步收尾日志输出完再出提示符:
    // zlmediakit 关流后 poller 线程的 TEARDOWN/析构走自带日志系统, 不经 avox 观察者,
    // 会洒在提示符后。等一会儿让它们先打完, 提示符再出现就清晰了。
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    if (rc != 0) {
      printf("Command returned: %d\n", rc);
    }
  }
  // 清理 Ctrl+C 处理器, 恢复日志
  removeShellCtrlHandler();
  setLogObserver(nullptr);
  printf("Bye.\n");
  return 0;
}

}
