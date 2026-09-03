#include "CmdHelper.hpp"

#include <condition_variable>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#endif

#include "avox/AvoxLog.h"
#include "avox/module/Time.hpp"
#include "avox_cmd/ConsoleWin32.hpp"

namespace avox {

std::atomic<bool> gCmdRunning{true};

// Ctrl+C 拦截钩子 (raw line editor 下 SetConsoleCtrlHandler 收不到 Ctrl+C — cmdEnterRaw 关了
// ENABLE_PROCESSED_INPUT; readLineRawWin 自检 ctrl+vk=='C' 触发本钩子让宿主 cancel/退出)。
// 跨平台声明 (hpp), 各平台 readLineRaw 都可调; 仅文件内持有。
static std::function<void()> gCtrlCInterceptor;
void setCtrlCInterceptor(std::function<void()> hook) { gCtrlCInterceptor = std::move(hook); }

void cmdSignalHandler(int sig) {
  if (sig == SIGINT || sig == SIGTERM) {
    gCmdRunning = false;
  }
}

void ensureDir(const std::string& dir) {
  if (dir.empty()) return;
#ifdef _WIN32
  CreateDirectoryA(dir.c_str(), nullptr);
#else
  mkdir(dir.c_str(), 0777);
#endif
}

FileLogOb::FileLogOb(const std::string& path) {
  file.open(path, std::ios::out | std::ios::app);
}

FileLogOb::~FileLogOb() {
  if (file.is_open()) file.close();
}

bool FileLogOb::isOpen() const { return file.is_open(); }

void FileLogOb::onLogEvent(int level, const char* message) {
  if (!file.is_open()) return;
  std::lock_guard<std::mutex> lock(mtx);
  // level: 0=info, 1=warn, 2=error, 3=debug
  const char* tag = "info";
  if (level == 1)
    tag = "warn";
  else if (level == 2)
    tag = "error";
  else if (level == 3)
    tag = "debug";
  file << "[" << formatLogTime() << "] [" << tag << "] " << message << std::endl;
}

bool cmdIsInteractive() {
#ifdef _WIN32
  return GetFileType(GetStdHandle(STD_INPUT_HANDLE)) == FILE_TYPE_CHAR;
#else
  return isatty(fileno(stdin)) == 1;
#endif
}

void cmdClearScreen() {
  // VT 启用后用 ANSI 清屏 (Win 需先 cmdEnableVt; 否则旧 conhost 不识别转义)。
  // 比 system("cls") 快、无子进程开销, POSIX 天然兼容。
  printf("\033[2J\033[H");
  fflush(stdout);
}

bool cmdReadLine(std::string& out) {
  out.clear();
#ifdef _WIN32
  HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
  if (GetFileType(hIn) == FILE_TYPE_CHAR) {
    // 控制台: ReadConsoleW 单次读整行(大缓冲, 不走循环), 转 UTF-8
    const DWORD kCap = 8192;
    wchar_t wbuf[kCap];
    DWORD read = 0;
    if (!ReadConsoleW(hIn, wbuf, kCap - 1, &read, nullptr)) {
      return false;   // EOF (Ctrl-Z+Enter) 或失败
    }
    int len = WideCharToMultiByte(CP_UTF8, 0, wbuf, (int)read, nullptr, 0,
                                  nullptr, nullptr);
    if (len > 0) {
      out.resize((size_t)len);
      WideCharToMultiByte(CP_UTF8, 0, wbuf, (int)read, &out[0], len, nullptr,
                          nullptr);
    }
    // 去尾部 \r\n
    while (!out.empty() && (out.back() == '\r' || out.back() == '\n')) {
      out.pop_back();
    }
    return true;
  }
#endif
  // 非控制台(管道/重定向)或非 Windows: std::getline
  return static_cast<bool>(std::getline(std::cin, out));
}

bool cmdEnableVt() {
#ifdef _WIN32
  // UTF-8 代码页: 中文路径/回答正确显示 (动态解析见 ConsoleWin32.hpp)
  conSetUtf8Codepage();
  // 输出侧 VT: ANSI 颜色/光标转义在 conhost 生效 (Win10 1607+)
  HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
  DWORD mode = 0;
  if (!GetConsoleMode(hOut, &mode)) return false;
  mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
  return SetConsoleMode(hOut, mode) != FALSE;
#else
  // POSIX 终端默认识别 ANSI 转义; locale 由调用方设置
  return true;
#endif
}

int cmdTerminalWidth() {
#ifdef _WIN32
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  if (conGetScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
    int w = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    if (w > 0) return w;
  }
  return 80;
#else
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
    return (int)ws.ws_col;
  return 80;
#endif
}

// ========== raw 输入模式 (line editor 自管按键) ==========

namespace {
#ifdef _WIN32
DWORD gSavedInMode = 0;
HANDLE gSavedInHandle = nullptr;
bool gInRaw = false;
bool gInQuiet = false;
#else
struct termios gSavedTerm;
bool gTermSaved = false;
#endif
CmdCompleter gCompleter = nullptr;
}  // namespace

void cmdRegisterCompleter(CmdCompleter fn) { gCompleter = fn; }

bool cmdEnterRaw() {
#ifdef _WIN32
  HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
  if (GetFileType(hIn) != FILE_TYPE_CHAR) return false;
  DWORD mode = 0;
  if (!GetConsoleMode(hIn, &mode)) return false;
  if (!gInRaw) {                 // 首次 enter 才保存, 防嵌套覆盖
    gSavedInMode = mode;
    gSavedInHandle = hIn;
    gInRaw = true;
  }
  DWORD raw = mode;
  raw &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT);
  // 不设 ENABLE_VIRTUAL_TERMINAL_INPUT: 该标志让 conhost 以 VT 转义序列报告输入,
  // 与 ReadConsoleInputW 的 KEY_EVENT 互斥; 启用后 Backspace 等键不再产生
  // 正确的 KEY_EVENT, 导致 raw line editor 按键处理异常 (Backspace 被当空格插入)。
  return SetConsoleMode(hIn, raw) != FALSE;
#else
  if (!isatty(STDIN_FILENO)) return false;
  struct termios t;
  if (tcgetattr(STDIN_FILENO, &t) != 0) return false;
  if (!gTermSaved) {
    gSavedTerm = t;
    gTermSaved = true;
  }
  struct termios raw = t;
  raw.c_lflag &= ~(ICANON | ECHO | ECHOE | ECHOK | ECHONL | ISIG | IEXTEN);
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag |= CS8;
  raw.c_cc[VMIN] = 1;
  raw.c_cc[VTIME] = 0;
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) return false;
  // 开启 bracketed paste: 告诉终端粘贴时用 \x1b[2004~ ... \x1b[2004~ 包裹,
  // line editor 据此把粘贴内容当作一次输入, 换行不触发提交。
  write(STDOUT_FILENO, "\x1b[?2004h", 8);
  return true;
#endif
}

void cmdExitRaw() {
#ifdef _WIN32
  if (!gInRaw || !gSavedInHandle) return;
  SetConsoleMode(gSavedInHandle, gSavedInMode);
  gInRaw = false;
  // raw exit 恢复原始模式可能覆盖 quiet 的增量修改 (QuickEdit/Mouse 位);
  // 根据当前 quiet 状态修正: quiet active 则关, 否则开。
  {
    DWORD mode = 0;
    if (GetConsoleMode(gSavedInHandle, &mode)) {
      if (gInQuiet) {
        mode &= ~ENABLE_QUICK_EDIT_MODE;
        mode &= ~ENABLE_MOUSE_INPUT;
      } else {
        mode |= ENABLE_QUICK_EDIT_MODE;
        mode |= ENABLE_MOUSE_INPUT;
      }
      mode |= ENABLE_EXTENDED_FLAGS;
      SetConsoleMode(gSavedInHandle, mode);
    }
  }
#else
  if (!gTermSaved) return;
  // 关闭 bracketed paste
  write(STDOUT_FILENO, "\x1b[?2004l", 8);
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &gSavedTerm);
  gTermSaved = false;
#endif
}

// ========== quiet-input 模式 (关 QuickEdit 选区 + 鼠标事件, 防点击终端卡死输出) ==========
// Windows 经典控制台 (conhost) 在鼠标点击/框选时会阻塞本进程的控制台输出写。长 running
// 的交互程序 (voice 录音循环 / agent spinner) 点一下终端, 输出线程就卡在 WriteConsole
// 上 → 卡死。关掉 QuickEdit 选区 + ENABLE_MOUSE_INPUT 后, 点击终端不再干扰输出。
// 与 cmdEnterRaw 同为 input 句柄模式改动, 但采用增量修改 (只关/开 QuickEdit+Mouse 位),
// 不保存-恢复整个模式, 避免与 raw 模式的 per-readline 切换互相踩 (raw exit 恢复原始模式
// 会覆盖 quiet 的修改, 或 quiet exit 恢复到 raw 模式但 raw 已不 active)。
// POSIX 无此问题, no-op。非控制台 (管道/重定向) 返回 false。

bool cmdEnterQuietInput() {
#ifdef _WIN32
  HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
  if (GetFileType(hIn) != FILE_TYPE_CHAR) return false;
  DWORD mode = 0;
  if (!GetConsoleMode(hIn, &mode)) return false;
  if (gInQuiet) return true;            // 已在 quiet, 幂等
  gInQuiet = true;
  mode &= ~ENABLE_QUICK_EDIT_MODE;      // 关闭鼠标快速编辑选区
  mode &= ~ENABLE_MOUSE_INPUT;          // 不接收鼠标事件, 避免点击往输入缓冲塞事件
  mode |= ENABLE_EXTENDED_FLAGS;        // 必须置上, 否则 QuickEdit 位不生效
  return SetConsoleMode(hIn, mode) != FALSE;
#else
  return false;
#endif
}

void cmdExitQuietInput() {
#ifdef _WIN32
  if (!gInQuiet) return;
  gInQuiet = false;
  HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
  DWORD mode = 0;
  if (!GetConsoleMode(hIn, &mode)) return;
  mode |= ENABLE_QUICK_EDIT_MODE;       // 恢复鼠标快速编辑选区
  mode |= ENABLE_MOUSE_INPUT;           // 恢复鼠标事件
  mode |= ENABLE_EXTENDED_FLAGS;        // 保持 (QuickEdit 依赖此位)
  SetConsoleMode(hIn, mode);
#endif
}

// ========== 控制台写线程 ==========
// 见 CmdHelper.hpp 头部说明。状态机: running=true 时业务线程 cmdWrite 入队, 写线程逐条
// fwrite+fflush; running=false 且队列排空后写线程退出 (Stop = 排干 + join)。
// flushed 计数单调递增, cmdWriteFlush 用「入队序号 <= flushed」做 drain 屏障 — 单写线程
// 保证序号即完成顺序。

namespace {
constexpr size_t kMaxPendingBytes = 16 * 1024 * 1024;  // 框选积压上限 16MB

struct ConsoleWriter {
  std::mutex mtx;
  std::condition_variable cvItem;   // 写线程等新内容 / 恢复 / 退出通知
  std::condition_variable cvDrain;  // flush 等排空 / backpressure 等回落
  std::condition_variable cvHold;   // Hold 等上一个持有者 Release
  std::deque<std::string> queue;
  std::thread thread;
  std::atomic<bool> running{false};
  bool held = false;                // fd1 捕获中: 写线程停写, 新内容只入队
  bool writerBusy = false;          // 写线程正 fwrite 一条 (Hold 等它完成)
  size_t pendingBytes = 0;          // 当前排队字节 (backpressure 判定)
  uint64_t pushed = 0;              // 累计入队条数 (drain 屏障目标)
  uint64_t flushed = 0;             // 累计落屏条数
};

ConsoleWriter gWriter;

void writerLoop() {
  while (true) {
    std::string item;
    {
      std::unique_lock<std::mutex> lock(gWriter.mtx);
      gWriter.cvItem.wait(lock, [] {
        return (!gWriter.queue.empty() && !gWriter.held)
               || !gWriter.running.load();
      });
      const bool canPop = !gWriter.queue.empty() && !gWriter.held;
      if (!canPop) {
        if (!gWriter.running.load() && gWriter.queue.empty()) break;  // 停止且排干
        continue;  // held 挂起中: 等恢复
      }
      item = std::move(gWriter.queue.front());
      gWriter.queue.pop_front();
      gWriter.pendingBytes -= item.size();
      gWriter.writerBusy = true;  // 拿锁期间标记, 出锁后 fwrite 可能与 Hold 并发
    }
    std::fwrite(item.data(), 1, item.size(), stdout);
    std::fflush(stdout);
    {
      std::lock_guard<std::mutex> lock(gWriter.mtx);
      gWriter.writerBusy = false;
      ++gWriter.flushed;
    }
    gWriter.cvDrain.notify_all();
  }
}
}  // namespace

void cmdWriterStart() {
  std::lock_guard<std::mutex> lock(gWriter.mtx);
  if (gWriter.running.load()) return;
  gWriter.running.store(true);
  gWriter.thread = std::thread(writerLoop);
}

void cmdWriterStop() {
  std::thread joinable;
  {
    std::lock_guard<std::mutex> lock(gWriter.mtx);
    if (!gWriter.running.load()) return;
    gWriter.running.store(false);
    gWriter.held = false;  // 兜底解除挂起, 让写线程能排干退出
    joinable = std::move(gWriter.thread);
  }
  gWriter.cvItem.notify_all();
  if (joinable.joinable()) joinable.join();  // 写线程先排干队列再退出
}

void cmdWrite(const char* data, size_t len) {
  if (data == nullptr || len == 0) return;
  if (!gWriter.running.load()) {
    // 写线程未启动 (非交互外壳): 直写, 行为与原 printf+fflush 一致
    std::fwrite(data, 1, len, stdout);
    std::fflush(stdout);
    return;
  }
  std::string item(data, len);
  {
    std::unique_lock<std::mutex> lock(gWriter.mtx);
    // backpressure: 长时间框选把队列堆到上限时等写线程消化 (选区结束即恢复)
    gWriter.cvDrain.wait(lock, [] { return gWriter.pendingBytes < kMaxPendingBytes; });
    gWriter.pendingBytes += len;
    ++gWriter.pushed;
    gWriter.queue.push_back(std::move(item));
  }
  gWriter.cvItem.notify_one();
}

void cmdWrite(const char* text) {
  if (text == nullptr) return;
  cmdWrite(text, std::strlen(text));
}

void cmdWriteF(const char* fmt, ...) {
  char buf[4096];
  va_list args;
  va_start(args, fmt);
  const int len = std::vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  if (len < 0) return;
  if (static_cast<size_t>(len) < sizeof(buf)) {
    cmdWrite(buf, static_cast<size_t>(len));
    return;
  }
  // 超过栈缓冲 (长工具结果等): 精确长度重格式化一遍
  std::vector<char> big(static_cast<size_t>(len) + 1);
  va_start(args, fmt);
  std::vsnprintf(big.data(), big.size(), fmt, args);
  va_end(args);
  cmdWrite(big.data(), static_cast<size_t>(len));
}

void cmdWriteFlush() {
  if (!gWriter.running.load()) {
    std::fflush(stdout);
    return;
  }
  std::unique_lock<std::mutex> lock(gWriter.mtx);
  const uint64_t target = gWriter.pushed;
  // 单写线程按序 flush, flushed >= target ⇒ 此刻之前的入队内容全部落屏
  gWriter.cvDrain.wait(lock, [target] { return gWriter.flushed >= target; });
}

void cmdWriterHold() {
  if (!gWriter.running.load()) return;
  std::unique_lock<std::mutex> lock(gWriter.mtx);
  // 与其他持有者互斥 (并行工具各自捕获 fd1 时串行化)
  gWriter.cvHold.wait(lock, [] { return !gWriter.held; });
  gWriter.held = true;
  // 只等「正在写的那条」完成: 挂起后队列里的内容不会被写 (Hold 期间写线程停 pop),
  // Release 后按原序落屏; 不能等整个队列排空 — 写线程被挂起, 等排空会自锁。
  gWriter.cvDrain.wait(lock, [] { return !gWriter.writerBusy; });
}

void cmdWriterRelease() {
  {
    std::lock_guard<std::mutex> lock(gWriter.mtx);
    gWriter.held = false;
  }
  gWriter.cvItem.notify_all();
  gWriter.cvHold.notify_all();
}

// ========== CmdHistory ==========

void CmdHistory::load(const std::string& filePath) {
  path = filePath;
  entries.clear();
  std::ifstream f(path);
  if (!f.is_open()) return;
  std::string line;
  while (std::getline(f, line)) {
    if (line.empty()) continue;
    // 去重: 与最近一条相同则跳过
    if (!entries.empty() && entries.back() == line) continue;
    entries.push_back(std::move(line));
  }
  // 截断到 kMax
  if ((int)entries.size() > kMax) {
    entries.erase(entries.begin(), entries.begin() + (entries.size() - kMax));
  }
}

void CmdHistory::add(const std::string& line) {
  if (line.empty()) return;
  if (!entries.empty() && entries.back() == line) return;
  entries.push_back(line);
  if ((int)entries.size() > kMax) entries.erase(entries.begin());
  // append 写文件
  std::ofstream f(path, std::ios::app);
  if (f.is_open()) f << line << "\n";
}

const char* CmdHistory::browse(int offset) const {
  if (offset < 0 || offset >= (int)entries.size()) return nullptr;
  return entries[entries.size() - 1 - offset].c_str();
}

// ========== raw 行编辑器提示符覆盖 ==========
// 向导 (DeepSeek API Key / 新建配置) 期间由宿主置空, 避免背景输入线程的行编辑器渲染
// 出默认 "agent> " 盖住向导标签。renderLine 每次渲染都读当前覆盖, 因此对已阻塞在
// ReadConsoleInputW 的编辑器同样生效 (用户敲键时才渲染)。宿主只在输入线程阻塞等待时
// 改动, 无并发读问题。
static std::string gRawLinePromptOverride;         // 覆盖提示符内容 (通常为空串)
static bool gRawLinePromptOverrideActive = false;  // 覆盖是否生效

void setRawLinePromptOverride(const std::string& prompt) {
  gRawLinePromptOverride = prompt;
  gRawLinePromptOverrideActive = true;
}

void clearRawLinePromptOverride() {
  gRawLinePromptOverrideActive = false;
  gRawLinePromptOverride.clear();
}

// ========== line editor (raw 模式) ==========

namespace {
// 行编辑状态: 缓冲 + 光标 (UTF-8 码点感知) + 历史浏览 + 多行
struct LineEdit {
  std::string buf;       // UTF-8 行内容
  int cursor = 0;        // 光标位置 (UTF-8 码点偏移, 0..len)
  int histIdx = -1;      // -1=不在浏览历史; 0=最近一条
  std::string savedBuf;  // 进入历史浏览前的 buf (Esc/↓ 到底恢复)
  std::string prompt;    // 提示符 (如 "agent> ", renderLine 带上, 避免清行时消失)

  // UTF-8 码点计数
  static int utf8len(const std::string& s) {
    int n = 0;
    for (size_t i = 0; i < s.size();) {
      unsigned char c = (unsigned char)s[i];
      if (c < 0x80) { ++i; }
      else if (c < 0xE0) { i += 2; }
      else if (c < 0xF0) { i += 3; }
      else { i += 4; }
      ++n;
    }
    return n;
  }
  // 码点偏移 → 字节偏移
  static int utf8off(const std::string& s, int cp) {
    int n = 0;
    size_t i = 0;
    for (; i < s.size() && n < cp;) {
      unsigned char c = (unsigned char)s[i];
      if (c < 0x80) ++i;
      else if (c < 0xE0) i += 2;
      else if (c < 0xF0) i += 3;
      else i += 4;
      ++n;
    }
    return (int)i;
  }
  // 光标处插入一个 UTF-8 码点 (1-4 字节)
  void insert(const char* data, size_t len) {
    buf.insert(utf8off(buf, cursor), data, len);
    ++cursor;
  }
  // 删除光标前一个码点 (退格)
  void backspace() {
    if (cursor <= 0) return;
    int byteEnd = utf8off(buf, cursor);
    int byteStart = utf8off(buf, cursor - 1);
    buf.erase(byteStart, byteEnd - byteStart);
    --cursor;
  }
  // 删除光标后一个码点 (Delete)
  void del() {
    if (cursor >= utf8len(buf)) return;
    int byteStart = utf8off(buf, cursor);
    int byteEnd = utf8off(buf, cursor + 1);
    buf.erase(byteStart, byteEnd - byteStart);
  }
  // 光标移到码点偏移
  void moveCursor(int cp) {
    cursor = cp < 0 ? 0 : (cp > utf8len(buf) ? utf8len(buf) : cp);
  }
  // 清空
  void clear() {
    buf.clear();
    cursor = 0;
    histIdx = -1;
  }
};

// 渲染当前行: 清旧内容 -> 写提示符 + buf -> 定位光标
// 支持多行: 当 prompt+buf 超过终端宽度换行时, 先上移到首行, 清除到屏幕底部,
// 再写完整内容, 最后定位光标 (考虑换行和宽字符占列)。
void renderLine(const LineEdit& le) {
  int termW = cmdTerminalWidth();
  if (termW <= 0) termW = 80;
  // 向导覆盖: 覆盖生效时用覆盖提示符 (通常是空串, 标签已由宿主打印在上方一行),
  // 否则用 le.prompt (如 "agent> ")。
  const std::string& dispPrompt =
      gRawLinePromptOverrideActive ? gRawLinePromptOverride : le.prompt;
  // 计算显示宽度: prompt (含 ANSI 转义, 只算可见列) + buf
  // prompt 中的 ANSI 转义不计列宽, 简化: 用 prompt 的可见字符数
  int promptVis = 0;
  bool inEsc = false;
  for (unsigned char c : dispPrompt) {
    if (c == 0x1b) { inEsc = true; continue; }
    if (inEsc) {
      if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) inEsc = false;
      continue;
    }
    ++promptVis;
  }
  // buf 的显示列宽 (宽字符 CJK 占 2 列)
  int bufVis = 0;
  for (size_t i = 0; i < le.buf.size();) {
    unsigned char c = (unsigned char)le.buf[i];
    size_t cpLen = 1;
    if (c < 0x80) {
      cpLen = 1;
      bufVis += 1;
    } else if (c < 0xE0) {
      cpLen = 2;
      bufVis += 2;   // CJK 等宽字符占 2 列
    } else if (c < 0xF0) {
      cpLen = 3;
      bufVis += 2;   // CJK 统一按 2 列
    } else {
      cpLen = 4;
      bufVis += 2;   // emoji 等按 2 列
    }
    i += cpLen;
  }
  int totalVis = promptVis + bufVis;
  int totalRows = (totalVis + termW - 1) / termW;
  if (totalRows < 1) totalRows = 1;
  // 上移到首行 (当前光标在末行), 逐行清除
  if (totalRows > 1) {
    printf("\033[%dA", totalRows - 1);   // 上移到首行
  }
  printf("\r\033[J");   // 从首行行首开始, 清除到屏幕底部 (覆盖所有旧行)
  // 写 prompt + buf
  printf("%s%s", dispPrompt.c_str(), le.buf.c_str());
  // 定位光标: 计算光标处的显示列, 换算成行列, 从末尾移回
  int cursorVis = promptVis;
  {
    int byteEnd = LineEdit::utf8off(le.buf, le.cursor);
    for (size_t i = 0; i < (size_t)byteEnd;) {
      unsigned char c = (unsigned char)le.buf[i];
      size_t cpLen = 1;
      if (c < 0x80) {
        cpLen = 1;
        cursorVis += 1;
      } else if (c < 0xE0) {
        cpLen = 2;
        cursorVis += 2;
      } else if (c < 0xF0) {
        cpLen = 3;
        cursorVis += 2;
      } else {
        cpLen = 4;
        cursorVis += 2;
      }
      i += cpLen;
    }
  }
  // 从末尾位置 (totalVis 列) 左移到 cursorVis 列
  int moveLeft = totalVis - cursorVis;
  if (moveLeft > 0) {
    printf("\033[%dD", moveLeft);
  }
  fflush(stdout);
}

// Tab 补全: 调 gCompleter, 有唯一公共前缀则补入, 多候选则显示
void doTabComplete(LineEdit& le) {
  if (!gCompleter) return;
  // 取光标前的部分作 partial
  int byteEnd = LineEdit::utf8off(le.buf, le.cursor);
  std::string partial(le.buf, 0, (size_t)byteEnd);
  auto candidates = gCompleter(partial.c_str());
  if (candidates.empty()) return;
  if (candidates.size() == 1) {
    // 唯一候选: 补入 (替换 partial)
    le.buf = candidates[0] + le.buf.substr((size_t)byteEnd);
    le.cursor = LineEdit::utf8len(candidates[0]);
    renderLine(le);
    return;
  }
  // 多候选: 找公共前缀, 补入到分歧点; 显示候选列表
  size_t commonLen = candidates[0].size();
  for (size_t i = 1; i < candidates.size(); ++i) {
    size_t j = 0;
    while (j < commonLen && j < candidates[i].size() &&
           candidates[0][j] == candidates[i][j])
      ++j;
    commonLen = j;
    if (commonLen == 0) break;
  }
  if (commonLen > (size_t)byteEnd) {
    std::string prefix = candidates[0].substr(0, commonLen);
    le.buf = prefix + le.buf.substr((size_t)byteEnd);
    le.cursor = LineEdit::utf8len(prefix);
  }
  renderLine(le);
  // 显示候选 (每行一个, 最多 8 个)
  printf("\n");
  int show = (int)candidates.size() > 8 ? 8 : (int)candidates.size();
  for (int i = 0; i < show; ++i) printf("  %s\n", candidates[i].c_str());
  if ((int)candidates.size() > 8) printf("  ... (%d more)\n", (int)candidates.size() - 8);
  fflush(stdout);
}

#ifdef _WIN32
// Windows: ReadConsoleInputW 事件驱动 line editor
// 粘贴检测: conhost 粘贴时每个字符以 VK_PACKET (0xE7) 事件投递 (UnicodeChar=实际字符);
// 末尾追加一个 VK_RETURN。检测到 VK_PACKET 后, 一次性排空缓冲区里所有连续 VK_PACKET
// + 末尾 VK_RETURN, 合并为一次插入; 换行变空格, 粘贴的 Enter 不触发提交 (用户手动 Enter)。
// 这从根本上解决粘贴长文本 (含换行) 被拆成多次提交的问题。
bool readLineRawWin(LineEdit& le, CmdHistory* hist, bool& shiftEnter) {
  shiftEnter = false;
  HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
  while (true) {
    INPUT_RECORD rec;
    DWORD got = 0;
    if (!ReadConsoleInputW(hIn, &rec, 1, &got) || got == 0) return false;
    if (rec.EventType != KEY_EVENT) continue;
    const KEY_EVENT_RECORD& k = rec.Event.KeyEvent;
    if (!k.bKeyDown) continue;
    wchar_t ch = k.uChar.UnicodeChar;
    WORD vk = k.wVirtualKeyCode;
    DWORD ctl = k.dwControlKeyState;
    bool shift = (ctl & SHIFT_PRESSED) != 0;
    bool ctrl = (ctl & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
    // ---- 粘贴处理 (VK_PACKET) ----
    // conhost 粘贴: 每字符一个 VK_PACKET 事件, 末尾追加 VK_RETURN。
    // 检测到 VK_PACKET → 一次性排空后续所有 VK_PACKET + 末尾 VK_RETURN, 合并插入。
    if (vk == VK_PACKET) {
      std::wstring wPaste;
      wPaste += ch;
      // 循环读取后续事件, 收集连续 VK_PACKET 字符 + 吞掉末尾 VK_RETURN
      while (true) {
        // 先 peek 看下一个事件是什么, 不消费
        INPUT_RECORD peek;
        DWORD peekGot = 0;
        if (!conPeekConsoleInputW(hIn, &peek, 1, &peekGot) || peekGot == 0) break;
        if (peek.EventType != KEY_EVENT) {
          // 非 KEY_EVENT (如 WINDOW_BUFFER_SIZE): 消费掉, 继续看下一个
          ReadConsoleInputW(hIn, &peek, 1, &peekGot);
          continue;
        }
        const auto& pk = peek.Event.KeyEvent;
        if (!pk.bKeyDown) {
          // key-up: 消费掉, 继续看下一个
          ReadConsoleInputW(hIn, &peek, 1, &peekGot);
          continue;
        }
        if (pk.wVirtualKeyCode == VK_PACKET) {
          // 粘贴字符: 消费 + 收集
          ReadConsoleInputW(hIn, &peek, 1, &peekGot);
          wPaste += pk.uChar.UnicodeChar;
          continue;
        }
        if (pk.wVirtualKeyCode == VK_RETURN) {
          // 粘贴末尾的 Enter: 消费 (吞掉, 不触发提交)
          ReadConsoleInputW(hIn, &peek, 1, &peekGot);
          break;
        }
        // 其他 key-down (方向键/退格等): 粘贴结束, 不消费, 留给主循环
        break;
      }
      // 粘贴内容转 UTF-8, 换行变空格, 一次性插入行编辑缓冲区
      if (!wPaste.empty()) {
        int len = WideCharToMultiByte(CP_UTF8, 0, wPaste.c_str(), (int)wPaste.size(),
                                      nullptr, 0, nullptr, nullptr);
        if (len > 0) {
          std::string utf8(len, '\0');
          WideCharToMultiByte(CP_UTF8, 0, wPaste.c_str(), (int)wPaste.size(),
                              &utf8[0], len, nullptr, nullptr);
          // 换行变空格 (粘贴内容中的 \r\n 统一为单个空格, 避免多行拆分)
          std::string cleaned;
          cleaned.reserve(utf8.size());
          bool lastWasNewline = false;
          for (unsigned char c : utf8) {
            if (c == '\r' || c == '\n') {
              if (!lastWasNewline) { cleaned += ' '; lastWasNewline = true; }
            } else {
              cleaned += (char)c;
              lastWasNewline = false;
            }
          }
          // 去尾部空格 (原换行产生的)
          while (!cleaned.empty() && cleaned.back() == ' ') cleaned.pop_back();
          if (!cleaned.empty()) {
            // 一次性插入: 逐码点插入以正确维护 cursor
            for (size_t i = 0; i < cleaned.size();) {
              unsigned char bc = (unsigned char)cleaned[i];
              size_t cpLen = 1;
              if (bc >= 0xC0 && bc < 0xE0) cpLen = 2;
              else if (bc >= 0xE0 && bc < 0xF0) cpLen = 3;
              else if (bc >= 0xF0) cpLen = 4;
              le.insert(cleaned.c_str() + i, cpLen);
              i += cpLen;
            }
            renderLine(le);
          }
        }
      }
      continue;   // 粘贴完成, 等用户手动 Enter 提交
    }
    // Ctrl+C: 提交当前行 (handler 另路 cancel, 此处仅结束 readLineRaw)
    if (ctrl && vk == 'C') {
      if (gCtrlCInterceptor) gCtrlCInterceptor();   // raw 下不进 console handler, 触发宿主 cancel
      break;
    }
    // Ctrl+V: 读剪贴板粘贴 (经典 conhost 下终端不拦截 Ctrl+V, 否则会落进
    // 普通字符分支被当字母 v 插入)。Windows Terminal 的 Ctrl+V 由终端自身
    // 拦截并注入普通 KEY_EVENT, 不会走到这里, 故无重复粘贴。
    if (ctrl && vk == 'V') {
      std::wstring wClip;
      if (OpenClipboard(nullptr)) {
        HANDLE hData = GetClipboardData(CF_UNICODETEXT);
        if (hData) {
          const wchar_t* p = (const wchar_t*)GlobalLock(hData);
          if (p) {
            wClip = p;
            GlobalUnlock(hData);
          }
        }
        CloseClipboard();
      }
      if (!wClip.empty()) {
        // 与 VK_PACKET 粘贴一致: 转 UTF-8, 换行变空格, 逐码点插入
        int len = WideCharToMultiByte(CP_UTF8, 0, wClip.c_str(), (int)wClip.size(),
                                      nullptr, 0, nullptr, nullptr);
        if (len > 0) {
          std::string utf8(len, '\0');
          WideCharToMultiByte(CP_UTF8, 0, wClip.c_str(), (int)wClip.size(),
                              &utf8[0], len, nullptr, nullptr);
          std::string cleaned;
          cleaned.reserve(utf8.size());
          bool lastWasNewline = false;
          for (unsigned char c : utf8) {
            if (c == '\r' || c == '\n') {
              if (!lastWasNewline) { cleaned += ' '; lastWasNewline = true; }
            } else {
              cleaned += (char)c;
              lastWasNewline = false;
            }
          }
          while (!cleaned.empty() && cleaned.back() == ' ') cleaned.pop_back();
          if (!cleaned.empty()) {
            for (size_t i = 0; i < cleaned.size();) {
              unsigned char bc = (unsigned char)cleaned[i];
              size_t cpLen = 1;
              if (bc >= 0xC0 && bc < 0xE0) cpLen = 2;
              else if (bc >= 0xE0 && bc < 0xF0) cpLen = 3;
              else if (bc >= 0xF0) cpLen = 4;
              le.insert(cleaned.c_str() + i, cpLen);
              i += cpLen;
            }
            renderLine(le);
          }
        }
      }
      continue;
    }
    // Enter: Shift+Enter → 多行换行; 普通 Enter → 提交
    if (vk == VK_RETURN) {
      if (shift) {
        shiftEnter = true;
        le.insert("\n", 1);
        renderLine(le);
        continue;
      }
      break;
    }
    // 退格
    if (vk == VK_BACK) { le.backspace(); renderLine(le); continue; }
    // Delete
    if (vk == VK_DELETE) { le.del(); renderLine(le); continue; }
    // Esc: 清行
    if (vk == VK_ESCAPE) { le.clear(); renderLine(le); continue; }
    // 左右箭头
    if (vk == VK_LEFT) { le.moveCursor(le.cursor - 1); renderLine(le); continue; }
    if (vk == VK_RIGHT) { le.moveCursor(le.cursor + 1); renderLine(le); continue; }
    // Home / End
    if (vk == VK_HOME) { le.moveCursor(0); renderLine(le); continue; }
    if (vk == VK_END) { le.moveCursor(LineEdit::utf8len(le.buf)); renderLine(le); continue; }
    // ↑: 历史浏览 (更早)
    if (vk == VK_UP && hist) {
      if (le.histIdx < 0) { le.savedBuf = le.buf; le.histIdx = 0; }
      else if (le.histIdx < hist->size() - 1) ++le.histIdx;
      const char* entry = hist->browse(le.histIdx);
      if (entry) { le.buf = entry; le.cursor = LineEdit::utf8len(le.buf); renderLine(le); }
      continue;
    }
    // ↓: 历史浏览 (更新 / 恢复)
    if (vk == VK_DOWN && hist) {
      if (le.histIdx > 0) { --le.histIdx; const char* e = hist->browse(le.histIdx); if (e) { le.buf = e ? e : le.savedBuf; } }
      else { le.histIdx = -1; le.buf = le.savedBuf; }
      le.cursor = LineEdit::utf8len(le.buf);
      renderLine(le);
      continue;
    }
    // Tab: 补全
    if (vk == VK_TAB) { doTabComplete(le); continue; }
    // 普通字符 (含 IME 合成完成): 排除控制字符 (< 0x20, 含 BS=0x08/DEL 等), 它们由上面的
    // vk 分支处理; 否则 raw 模式下 BS 的 ch=0x08 会落入此处被插入为"空格"字符。
    // 批量收集: 粘贴长文本时 (Windows Terminal Ctrl+V 等) 字符以普通 KEY_EVENT 快速投递
    // (非 VK_PACKET), 逐字符 insert+renderLine 会导致终端来不及处理 \r 覆盖, 产生 N 行
    // 重复输出。先 peek 收集连续普通字符, 一次性插入 + 单次 renderLine。
    if (ch != 0 && ch >= 0x20) {
      std::wstring wBatch;
      wBatch += ch;
      // peek 后续: 连续普通可打印字符 (非控制键) → 收集, 减少渲染次数
      while (true) {
        INPUT_RECORD peek;
        DWORD peekGot = 0;
        if (!conPeekConsoleInputW(hIn, &peek, 1, &peekGot) || peekGot == 0) break;
        if (peek.EventType != KEY_EVENT) break;
        const auto& pk = peek.Event.KeyEvent;
        if (!pk.bKeyDown) {
          // key-up: 消费掉, 继续看下一个
          ReadConsoleInputW(hIn, &peek, 1, &peekGot);
          continue;
        }
        wchar_t pch = pk.uChar.UnicodeChar;
        WORD pvk = pk.wVirtualKeyCode;
        // 只收集普通可打印字符 (非控制键/方向键/功能键/VK_PACKET)
        if (pch >= 0x20 && pvk != VK_PACKET && pvk != VK_RETURN &&
            pvk != VK_BACK && pvk != VK_DELETE && pvk != VK_ESCAPE &&
            pvk != VK_TAB && pvk != VK_LEFT && pvk != VK_RIGHT &&
            pvk != VK_UP && pvk != VK_DOWN && pvk != VK_HOME && pvk != VK_END) {
          ReadConsoleInputW(hIn, &peek, 1, &peekGot);
          wBatch += pch;
          continue;
        }
        // 遇到非普通字符: 停止收集, 留给主循环处理
        break;
      }
      // 批量转 UTF-8 并插入
      if (wBatch.size() == 1) {
        // 单字符: 直接插入 (常见打字场景, 无额外开销)
        char mb[8];
        int n = WideCharToMultiByte(CP_UTF8, 0, &ch, 1, mb, sizeof(mb), nullptr, nullptr);
        if (n > 0) le.insert(mb, (size_t)n);
      } else {
        // 批量: 转 UTF-8, 逐码点插入 (与 VK_PACKET 粘贴处理一致)
        int len = WideCharToMultiByte(CP_UTF8, 0, wBatch.c_str(), (int)wBatch.size(),
                                      nullptr, 0, nullptr, nullptr);
        if (len > 0) {
          std::string utf8(len, '\0');
          WideCharToMultiByte(CP_UTF8, 0, wBatch.c_str(), (int)wBatch.size(),
                              &utf8[0], len, nullptr, nullptr);
          for (size_t i = 0; i < utf8.size();) {
            unsigned char bc = (unsigned char)utf8[i];
            size_t cpLen = 1;
            if (bc >= 0xC0 && bc < 0xE0) cpLen = 2;
            else if (bc >= 0xE0 && bc < 0xF0) cpLen = 3;
            else if (bc >= 0xF0) cpLen = 4;
            le.insert(utf8.c_str() + i, cpLen);
            i += cpLen;
          }
        }
      }
      renderLine(le);
    }
  }
  return true;
}
#else
// POSIX: read() + 转义序列解析 line editor
// 支持 bracketed paste: 终端在粘贴内容前后包裹 \x1b[2004~ 和 \x1b[2004~,
// 粘贴期间 \n/\r 不触发提交, 换行变空格, 粘贴结束后一次性插入。
bool readLineRawPosix(LineEdit& le, CmdHistory* hist, bool& shiftEnter) {
  shiftEnter = false;
  enum EscState { None, Esc, EscBracket };
  EscState escState = None;
  bool inPaste = false;   // bracketed paste 进行中
  while (true) {
    char c;
    ssize_t n = ::read(STDIN_FILENO, &c, 1);
    if (n <= 0) return false;
    // 转义序列状态机
    if (escState == None && c == 0x1b) { escState = Esc; continue; }
    if (escState == Esc) {
      if (c == '[') { escState = EscBracket; continue; }
      // 单独 Esc: 清行 (粘贴中忽略 Esc)
      if (!inPaste) { le.clear(); renderLine(le); }
      escState = None; continue;
    }
    if (escState == EscBracket) {
      escState = None;
      // ---- bracketed paste: \x1b[2004~ (开始) / \x1b[2004~ (结束) ----
      if (c == '2') {
        // 可能是 2004~ (bracketed paste start/end): 读后续 3 字符 "004~"
        char d[3] = {};
        if (::read(STDIN_FILENO, &d[0], 1) == 1 && d[0] == '0' &&
            ::read(STDIN_FILENO, &d[1], 1) == 1 && d[1] == '0' &&
            ::read(STDIN_FILENO, &d[2], 1) == 1 && d[2] == '4') {
          char tilde = 0;
          if (::read(STDIN_FILENO, &tilde, 1) == 1 && tilde == '~') {
            inPaste = !inPaste;   // 2004~: 开始/结束切换
            continue;
          }
        }
        // 不是 2004~, 丢弃已读的字符 (无法放回)
        continue;
      }
      switch (c) {
        case 'A':  // ↑
          if (hist) {
            if (le.histIdx < 0) { le.savedBuf = le.buf; le.histIdx = 0; }
            else if (le.histIdx < hist->size() - 1) ++le.histIdx;
            const char* e = hist->browse(le.histIdx);
            if (e) { le.buf = e; le.cursor = LineEdit::utf8len(le.buf); renderLine(le); }
          }
          continue;
        case 'B':  // ↓
          if (hist) {
            if (le.histIdx > 0) { --le.histIdx; const char* e = hist->browse(le.histIdx); { le.buf = e ? e : le.savedBuf; } }
            else { le.histIdx = -1; le.buf = le.savedBuf; }
            le.cursor = LineEdit::utf8len(le.buf); renderLine(le);
          }
          continue;
        case 'C':  // →
          le.moveCursor(le.cursor + 1); renderLine(le); continue;
        case 'D':  // ←
          le.moveCursor(le.cursor - 1); renderLine(le); continue;
        case 'H':  // Home
          le.moveCursor(0); renderLine(le); continue;
        case 'F':  // End
          le.moveCursor(LineEdit::utf8len(le.buf)); renderLine(le); continue;
        case '3':  // Delete (ESC[3~)
          ::read(STDIN_FILENO, &c, 1);  // 消耗 '~'
          le.del(); renderLine(le); continue;
        default: continue;  // 未知序列, 丢弃
      }
    }
    // Ctrl+C
    if (c == 0x03) break;
    // Enter: 粘贴中换行变空格; 非粘贴时提交
    if (c == '\n' || c == '\r') {
      if (inPaste) {
        // 粘贴中的换行: 插入空格 (连续 \r\n 只产一个空格)
        // 简化: 直接插入空格
        le.insert(" ", 1);
        renderLine(le);
        continue;
      }
      break;
    }
    // 退格 / DEL (粘贴中忽略)
    if (c == 0x7f || (unsigned char)c == 0x08) {
      if (!inPaste) { le.backspace(); renderLine(le); }
      continue;
    }
    // Tab (粘贴中忽略)
    if (c == '\t') {
      if (!inPaste) doTabComplete(le);
      continue;
    }
    // 普通字符 (UTF-8, >= 0x20 可打印)
    if ((unsigned char)c >= 0x20 && (unsigned char)c < 0x7F) {
      le.insert(&c, 1);
      renderLine(le);
    } else if ((unsigned char)c >= 0xC0) {
      // 多字节 UTF-8 码点: 收集完整码点
      size_t need = 1;
      if ((unsigned char)c >= 0xC0 && (unsigned char)c < 0xE0) need = 2;
      else if ((unsigned char)c >= 0xE0 && (unsigned char)c < 0xF0) need = 3;
      else if ((unsigned char)c >= 0xF0) need = 4;
      char seq[4] = {c, 0, 0, 0};
      for (size_t i = 1; i < need; ++i) {
        if (::read(STDIN_FILENO, &seq[i], 1) != 1) break;
      }
      le.insert(seq, need);
      renderLine(le);
    }
  }
  return true;
}
#endif
}  // namespace

bool cmdReadLineRaw(std::string& out, CmdHistory* hist, const char* prompt,
                    const std::string& prefill) {
  out.clear();
  LineEdit le;
  if (prompt) le.prompt = prompt;
  if (!prefill.empty()) {
    le.buf = prefill;
    le.cursor = LineEdit::utf8len(le.buf);
    renderLine(le);  // 预填内容先渲染出来 (等键期间也要可见可改)
  }
  // 历史由调用方传入; nullptr 则无历史浏览
  bool shiftEnter = false;
#ifdef _WIN32
  bool ok = readLineRawWin(le, hist, shiftEnter);
#else
  bool ok = readLineRawPosix(le, hist, shiftEnter);
#endif
  if (!ok) return false;
  out = std::move(le.buf);
  fputc('\n', stdout);
  fflush(stdout);
  return true;
}

}
