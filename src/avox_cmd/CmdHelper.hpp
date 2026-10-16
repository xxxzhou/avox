#pragma once

#include <atomic>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "avox/AvoxLog.h"

namespace avox {

// 全局运行标志, SIGINT/SIGTERM 置 false 通知主循环退出
extern std::atomic<bool> gCmdRunning;

// 信号处理: Ctrl+C 等触发 gCmdRunning = false
void cmdSignalHandler(int sig);

// 确保目录存在 (已存在则忽略)
void ensureDir(const std::string& dir);

// 文件日志观察者: 将 avox 内部日志写出到文本文件
class FileLogOb : public ILogOb {
 public:
  explicit FileLogOb(const std::string& path);
  ~FileLogOb() override;
  bool isOpen() const;
  void onLogEvent(int level, const char* message) override;

 private:
  std::ofstream file;
  std::mutex mtx;
};

// stdin 是否为交互式终端 (控制台)
bool cmdIsInteractive();

// 清屏 (ANSI; Win 下需先调 cmdEnableVt 才生效)
void cmdClearScreen();

// 启用终端 VT 地基: Win 启用 ENABLE_VIRTUAL_TERMINAL_PROCESSING (ANSI 颜色/光标转义在
// conhost 生效) + UTF-8 代码页; POSIX no-op (终端默认识别 ANSI)。仅输出侧 — 不开
bool cmdEnableVt();

// 终端可视列宽 (软换行/进度条用); 取不到回退 80。
int cmdTerminalWidth();

// IO auto 选路: url 是否为 ZL 真支持的流式源 (rtsp/rtmp/srt/ws 直连;
// http(s) 仅认 .m3u8/.flv/.ts 流式后缀, 忽略 query)。普通 http 文件直链
bool bZlStreamUrl(const char* url);

// ========== ANSI 着色 (需先 cmdEnableVt 才在 Win conhost 生效) ==========
// SGR 转义码 (字体色): 30=黑 31=红 32=绿 33=黄 34=蓝 35=紫 36=青 37=白 90-97 亮色; 0=重置
// 常用色板: 各调用方语义化引用 (avox_agent: user/tool/error; avox_cli: help 着色)
namespace cmdColor {
constexpr const char* kReset   = "\033[0m";
constexpr const char* kRed     = "\033[31m";   // error / diff-
constexpr const char* kGreen   = "\033[32m";   // success / diff+
constexpr const char* kYellow  = "\033[33m";   // tool / 警告
constexpr const char* kBlue    = "\033[34m";   // user / 提示符
constexpr const char* kPurple  = "\033[35m";   // 关键字 / 标题
constexpr const char* kCyan    = "\033[36m";   // 路径 / 链接
constexpr const char* kGray    = "\033[90m";   // 次要文本 / 注释
constexpr const char* kBold    = "\033[1m";
constexpr const char* kDim     = "\033[2m";
}

// 进入 raw 输入模式: Win 关 cooked 行编辑 (ENABLE_LINE_INPUT/ECHO_INPUT/PROCESSED_INPUT);
// 不开 ENABLE_VIRTUAL_TERMINAL_INPUT (该标志让 conhost 以 VT 序列报告输入, 与
bool cmdEnterRaw();
// 恢复 cmdEnterRaw 之前的 input 模式 (cooked)。未 enter 过则 no-op。
void cmdExitRaw();

// RAII: 构造 enter, 析构 exit (仅当 enter 成功)。per-readline 切换用。
struct cmdRawGuard {
  cmdRawGuard() { ok_ = cmdEnterRaw(); }
  ~cmdRawGuard() {
    if (ok_) cmdExitRaw();
  }
  bool entered() const { return ok_; }

 private:
  bool ok_ = false;
};

// ========== quiet-input 模式 (关 QuickEdit 选区 + 鼠标事件) ==========
// Windows 经典控制台 (conhost) 在鼠标点击/框选时会阻塞本进程的控制台输出写; 长 running
bool cmdEnterQuietInput();
void cmdExitQuietInput();

// ========== 控制台写线程 (conhost QuickEdit 框选冻结输出的解耦) ==========
// conhost 在 QuickEdit 框选/点击期间会冻结本进程的控制台写 (WriteConsole 阻塞到选区
void cmdWriterStart();   // 启动写线程 (幂等); 交互外壳启动时调
void cmdWriterStop();    // 排空队列并停写线程 (退出前调)
void cmdWrite(const char* data, size_t len);  // 写一段 UTF-8 字节 (含转义/换行)
void cmdWrite(const char* text);              // strlen 版
void cmdWriteF(const char* fmt, ...);         // printf 风格, 格式化后经写线程
void cmdWriteFlush();     // 屏障: 已入队内容全部落屏后再返回 (后续直写/读输入前用)
// fd1 捕获专用: 要 _dup2/dup2 换掉 fd 1 捕获子进程/子命令输出时, 先 Hold 再换 fd,
// 恢复 fd 后 Release — 挂起期间写线程停写 (已在队列里的内容先排干), 否则队列里未落屏的
void cmdWriterHold();
void cmdWriterRelease();

// Ctrl+C 拦截钩子: cmdReadLineRaw (raw line editor) 下 console 不生成 CTRL_C_EVENT
// (cmdEnterRaw 关了 ENABLE_PROCESSED_INPUT), SetConsoleCtrlHandler 收不到 Ctrl+C。
void setCtrlCInterceptor(std::function<void()> hook);

// raw 行编辑器提示符覆盖: 交互向导 (DeepSeek API Key / 新建配置) 期间宿主置空,
// 避免背景输入线程的行编辑器渲染出默认 "agent> " 盖住向导标签。renderLine 每次渲染
void setRawLinePromptOverride(const std::string& prompt);
void clearRawLinePromptOverride();

// RAII: 构造 enter (关鼠标), 析构 exit (恢复)。整段交互会话包起来用, 出作用域
// (含提前 return/异常) 自动恢复, 不会漏。
struct cmdQuietInputGuard {
  cmdQuietInputGuard() { ok_ = cmdEnterQuietInput(); }
  ~cmdQuietInputGuard() {
    if (ok_) cmdExitQuietInput();
  }
  bool entered() const { return ok_; }

 private:
  bool ok_ = false;
};

// 命令历史: 持久化到 filePath (每次 append, 启动时加载)。avox_cli / avox_agent 各自指定路径。
class CmdHistory {
 public:
  // 从文件加载历史 (文件不存在/空则空历史; 每行一条, 去重, 最多 kMax 条)
  void load(const std::string& filePath);
  // 追加一行到历史 (去重: 与最近一条相同则不追加) 并写文件
  void add(const std::string& line);
  // 浏览: offset=0 最近一条, 1 倒数第二条...; 越界返回 nullptr
  const char* browse(int offset) const;
  int size() const { return (int)entries.size(); }

 private:
  static constexpr int kMax = 500;
  std::vector<std::string> entries;
  std::string path;
};

// raw 模式下读一行 (自管行编辑 + 回显, UTF-8)。完整版: 字符/退格/光标移动/↑↓历史/Tab补全/
// Shift+Enter 多行/Esc 清行。需先 cmdEnterRaw。成功返回 true (out 为该行, 不含换行);
bool cmdReadLineRaw(std::string& out, CmdHistory* hist = nullptr,
                    const char* prompt = nullptr,
                    const std::string& prefill = "");

// Tab 补全: 注册候选生成器 (返回候选列表), cmdReadLineRaw 按 Tab 调用最近注册的生成器。
// 多个调用方 (avox_cli 注册命令名, avox_agent 注册 / 命令 + 配置名) 各自注册。
using CmdCompleter = std::vector<std::string>(*)(const char* partial);
void cmdRegisterCompleter(CmdCompleter fn);

// 读一行控制台输入, 返回 UTF-8。
// Windows 用 ReadConsoleW 单次读整行(大缓冲), 让 conhost 的 cooked 行编辑器负责
bool cmdReadLine(std::string& out);

// 静默日志观察者: 吞掉所有 avox 日志, 避免和交互输入提示符/流式输出混在一起
class SilentLogOb : public ILogOb {
 public:
  void onLogEvent(int level, const char* message) override {}
};

}
