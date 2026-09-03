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
// ENABLE_VIRTUAL_TERMINAL_INPUT (会关 cooked 行编辑, 致现有 cmdReadLine 失效); 输入侧
// raw 切换见 cmdEnterRaw (P0-2)。返回是否成功启用。
bool cmdEnableVt();

// 终端可视列宽 (软换行/进度条用); 取不到回退 80。
int cmdTerminalWidth();

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
// ReadConsoleInputW 的 KEY_EVENT 互斥, 致 Backspace 等键处理异常)。POSIX termios cfmakeraw。
// 让方向键/Esc/Ctrl 组合等以原始事件进来, 供 line editor 自处理 (光标/历史/补全)。
// 仅 input 句柄, output 由 cmdEnableVt。非控制台 (管道/重定向) 返回 false (调用方回退
// std::getline)。配套 cmdExitRaw 恢复。
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
// 交互程序 (voice 录音循环) 点一下终端就会卡死输出。enter 后关掉 QuickEdit 选区 +
// ENABLE_MOUSE_INPUT, 点击终端不再干扰输出; exit 恢复。
// 注意: 关掉 QuickEdit 同时也废掉了用户框选复制/框选暂停滚动的能力 — 需要保留复制
// 的交互程序 (avox_agent 回复期) 应改用 cmdWriter 写线程解耦, 而不是进 quiet。
// 采用增量修改 (只关/开 QuickEdit+Mouse 位), 与 cmdEnterRaw 的 per-readline 保存-恢复
// 正交: raw exit 后会根据 quiet 状态修正 QuickEdit/Mouse 位, 互不踩。
// POSIX 无此问题 (no-op); 非控制台 (管道/重定向) enter 返回 false。
bool cmdEnterQuietInput();
void cmdExitQuietInput();

// ========== 控制台写线程 (conhost QuickEdit 框选冻结输出的解耦) ==========
// conhost 在 QuickEdit 框选/点击期间会冻结本进程的控制台写 (WriteConsole 阻塞到选区
// 结束)。若打印直接发生在关键线程 (agent 的 driver 回调 / spinner / 主循环), 用户框选
// 一下复制, 整轮回复就停在 WriteConsole 上 — 旧解法是回复期整体关 QuickEdit, 但那同时
// 废掉了框选复制与暂停滚动。现解法: 交互期把控制台写收敛到专职写线程, 业务线程只入队,
// 框选只冻「显示」(内容在队列里缓冲, 选区结束自动追平), 不再冻业务逻辑, QuickEdit
// 全程保持可用。未 start (未交互/管道) 时 cmdWrite 退化为直写 + fflush, 与原 printf
// 行为一致。backpressure: 长时间框选积压超过 kMaxPendingBytes 时生产方等待 (回到
// 「冻住」而不是无限吃内存)。
void cmdWriterStart();   // 启动写线程 (幂等); 交互外壳启动时调
void cmdWriterStop();    // 排空队列并停写线程 (退出前调)
void cmdWrite(const char* data, size_t len);  // 写一段 UTF-8 字节 (含转义/换行)
void cmdWrite(const char* text);              // strlen 版
void cmdWriteF(const char* fmt, ...);         // printf 风格, 格式化后经写线程
void cmdWriteFlush();     // 屏障: 已入队内容全部落屏后再返回 (后续直写/读输入前用)
// fd1 捕获专用: 要 _dup2/dup2 换掉 fd 1 捕获子进程/子命令输出时, 先 Hold 再换 fd,
// 恢复 fd 后 Release — 挂起期间写线程停写 (已在队列里的内容先排干), 否则队列里未落屏的
// 内容会写进捕获文件混进捕获结果。Hold 之间互斥 (并行捕获各自串行化), Release 必须与
// Hold 配对 (RAII 保证)。
void cmdWriterHold();
void cmdWriterRelease();

// Ctrl+C 拦截钩子: cmdReadLineRaw (raw line editor) 下 console 不生成 CTRL_C_EVENT
// (cmdEnterRaw 关了 ENABLE_PROCESSED_INPUT), SetConsoleCtrlHandler 收不到 Ctrl+C。
// readLineRawWin 自检 ctrl+vk=='C' 时触发本钩子, 让宿主 (avox_agent) 做 cancel/退出。
// 传 nullptr 清除。在输入线程触发 (非信号上下文, 可调复杂逻辑)。
void setCtrlCInterceptor(std::function<void()> hook);

// raw 行编辑器提示符覆盖: 交互向导 (DeepSeek API Key / 新建配置) 期间宿主置空,
// 避免背景输入线程的行编辑器渲染出默认 "agent> " 盖住向导标签。renderLine 每次渲染
// 都读当前覆盖, 对已阻塞在 ReadConsoleInputW 的编辑器同样生效。clear 恢复默认。
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
// EOF/失败返回 false。
// prefill 非空: 输入行预填该内容 (光标在末尾, 立即渲染), 用户可在预设基础上整行编辑
// (删参数/改参数/补值) 后回车提交。
bool cmdReadLineRaw(std::string& out, CmdHistory* hist = nullptr,
                    const char* prompt = nullptr,
                    const std::string& prefill = "");

// Tab 补全: 注册候选生成器 (返回候选列表), cmdReadLineRaw 按 Tab 调用最近注册的生成器。
// 多个调用方 (avox_cli 注册命令名, avox_agent 注册 / 命令 + 配置名) 各自注册。
using CmdCompleter = std::vector<std::string>(*)(const char* partial);
void cmdRegisterCompleter(CmdCompleter fn);

// 读一行控制台输入, 返回 UTF-8。
// Windows 用 ReadConsoleW 单次读整行(大缓冲), 让 conhost 的 cooked 行编辑器负责
// 回显/行编辑/IME 合成。**必须单次读**: 缓冲太小走循环、对同一行多次 ReadConsoleW 会
// 扰乱 conhost 行编辑器状态, 导致「IME 中文输入 + 粘贴长文本(如长 URL)」后回显/光标
// 错乱、后续敲键看不见 (输入仍能被捕获, 回车可提交)。其它平台或 stdin 被重定向(管道)
// 时用 std::getline。
// 成功返回 true (out 为该行, 不含换行); EOF(控制台 Ctrl-Z+Enter / 管道结束)/失败返回 false。
bool cmdReadLine(std::string& out);

// 静默日志观察者: 吞掉所有 avox 日志, 避免和交互输入提示符/流式输出混在一起
class SilentLogOb : public ILogOb {
 public:
  void onLogEvent(int level, const char* message) override {}
};

}
