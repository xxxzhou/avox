/**
 * @file tooltest.cpp
 * @brief avox_cli 黑盒冒烟测试 — 调用 avox_cli 传各种参数并显示输出
 *
 * 通过子进程拉起 avox_cli, 传入不同子命令/参数组合, 把每个用例的
 * stdout/stderr 输出和退出码原样打印出来, 用于端到端验证子命令分发、
 * 参数解析、帮助/版本输出, 以及真实播放 (zlmediakit/ffmpeg, 窗口/离屏)。
 *
 * Windows 下用 CreateProcessW 启动 (而非 _popen/cmd), 以便:
 *   1) 用 UTF-16 命令行正确传递中文媒体路径 (cmd 的 OEM 代码页会乱码);
 *   2) 直接拿子进程退出码, 不经 cmd.exe 的引号剥离规则。
 */

#include <cstdio>
#include <cstring>
#include <ctime>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#define CLI_NAME "avox_cli.exe"
#define PATH_SEP "\\"
#else
#include <unistd.h>
#define POPEN popen
#define PCLOSE pclose
#define CLI_NAME "avox_cli"
#define PATH_SEP "/"
#endif

// 真实播放测试用的本地媒体文件 (按需修改; 源码以 UTF-8 编译)
static const std::string kPlayFile = "D:/Back/美好_h265.mp4";

// 当前本地时间戳, 用于日志文件名 (月_日_时_分_秒)
static std::string logTimestamp() {
  std::time_t t = std::time(nullptr);
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  char buf[32] = {0};
  std::strftime(buf, sizeof(buf), "%m_%d_%H_%M_%S", &tm);
  return buf;
}

// 取本可执行文件所在目录 (avox_cli 与本程序由 avox_output 输出到同一目录)
static std::string exeDir() {
#ifdef _WIN32
  char path[MAX_PATH] = {0};
  DWORD n = GetModuleFileNameA(nullptr, path, MAX_PATH);
  if (n == 0 || n >= MAX_PATH) return ".";
#else
  char path[4096] = {0};
  ssize_t n = readlink("/proc/self/exe", path, sizeof(path) - 1);
  if (n <= 0) return ".";
#endif
  std::string p(path);
  size_t pos = p.find_last_of("\\/");
  return (pos != std::string::npos) ? p.substr(0, pos) : ".";
}

static std::string cliPath() { return exeDir() + PATH_SEP + CLI_NAME; }

// 总是加引号 (用于程序路径)
static std::string quote(const std::string& s) { return "\"" + s + "\""; }

// 仅当含空白时才加引号 (用于参数)
static std::string quoteIfSpace(const std::string& s) {
  if (s.find(' ') != std::string::npos || s.find('\t') != std::string::npos) {
    return "\"" + s + "\"";
  }
  return s;
}

#ifdef _WIN32
// UTF-8 字符串 -> UTF-16
static std::wstring toWide(const std::string& s) {
  if (s.empty()) return std::wstring();
  int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
  std::vector<wchar_t> buf(len);
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, buf.data(), len);
  return std::wstring(buf.data());
}

// Windows: CreateProcessW 启动 avox_cli, 合并捕获 stdout+stderr, 返回退出码
static int runProcess(const std::string& cli,
                      const std::vector<std::string>& args,
                      std::string& output) {
  std::string cmd = quote(cli);
  for (const auto& a : args) cmd += " " + quoteIfSpace(a);
  std::wstring wcmd = toWide(cmd);
  std::wstring wprog = toWide(cli);
  SECURITY_ATTRIBUTES sa;
  sa.nLength = sizeof(SECURITY_ATTRIBUTES);
  sa.lpSecurityDescriptor = nullptr;
  sa.bInheritHandle = TRUE;
  HANDLE hRead = nullptr;
  HANDLE hWrite = nullptr;
  if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return -1;
  SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);
  STARTUPINFOW si;
  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = hWrite;
  si.hStdError = hWrite;
  PROCESS_INFORMATION pi;
  ZeroMemory(&pi, sizeof(pi));
  BOOL ok = CreateProcessW(wprog.c_str(), &wcmd[0], nullptr, nullptr, TRUE, 0,
                           nullptr, nullptr, &si, &pi);
  CloseHandle(hWrite);
  if (!ok) {
    CloseHandle(hRead);
    return -1;
  }
  char buf[1024];
  DWORD n = 0;
  while (ReadFile(hRead, buf, sizeof(buf), &n, nullptr) && n > 0) {
    output.append(buf, n);
  }
  CloseHandle(hRead);
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD code = 1;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  return static_cast<int>(code);
}
#else
// POSIX: popen 启动
static int runProcess(const std::string& cli,
                      const std::vector<std::string>& args,
                      std::string& output) {
  std::string cmd = quote(cli);
  for (const auto& a : args) cmd += " " + quoteIfSpace(a);
  cmd += " 2>&1";
  FILE* pipe = POPEN(cmd.c_str(), "r");
  if (!pipe) return -1;
  char buf[1024];
  size_t n = 0;
  while ((n = fread(buf, 1, sizeof(buf), pipe)) > 0) {
    output.append(buf, n);
  }
  return PCLOSE(pipe);
}
#endif

// 运行一个用例并打印输出与退出码, 返回退出码
static int runCase(const std::string& title,
                   const std::vector<std::string>& args) {
  std::cout << "============================================" << std::endl;
  std::cout << "[CASE] " << title << std::endl;
  std::cout << "[CMD]  avox_cli";
  for (const auto& a : args) std::cout << " " << a;
  std::cout << std::endl;
  std::cout << "[OUT]" << std::endl;
  std::string output;
  int rc = runProcess(cliPath(), args, output);
  std::cout << output;  // 原样输出子进程字节
  if (!output.empty() && output.back() != '\n') std::cout << std::endl;
  std::cout << "[EXIT] " << rc << std::endl;
  return rc;
}

int main() {
  std::cout << "============================================" << std::endl;
  std::cout << "        avox_cli black-box smoke test        " << std::endl;
  std::cout << "============================================" << std::endl;
  std::cout << "avox_cli: " << cliPath() << std::endl;
  std::cout << std::endl;

  struct Case {
    std::string title;
    std::vector<std::string> args;
  };
  // 日志文件名带运行时间戳 (月_日_时_分_秒)
  std::string logFile = "D:/" + logTimestamp() + ".log";
  const std::vector<Case> cases = {
      // --- 子命令分发 / 参数解析 (无需媒体文件) ---
      {"无参数 -> 显示总帮助 (期望退出码 1)", {}},
      {"-help", {"-help"}},
      {"--help", {"--help"}},
      {"-version", {"-version"}},
      {"device -video -audio", {"device", "-video", "-audio"}},
      {"device -json", {"device", "-json"}},
      {"play -help (子命令帮助)", {"play", "-help"}},
      {"未知命令 foo (期望退出码 1)", {"foo"}},
      // --- 真实播放 (依赖 kPlayFile; 每个用 -t 限时, 防止无限等待) ---
      {"play [自动IO 离屏] -offscreen -t 5",
       {"play", "-i", kPlayFile, "-offscreen", "-t", "5"}},
      {"play [ffmpeg 离屏] -io ffmpeg -offscreen -t 5",
       {"play", "-i", kPlayFile, "-io", "ffmpeg", "-offscreen", "-t", "5"}},
      {"play [自动IO 窗口] 10秒自动关闭 -t 10",
       {"play", "-i", kPlayFile, "-t", "10"}},
      {"play [日志记录到文件] -log-file " + logFile + " -t 5",
       {"play", "-i", kPlayFile, "-log-file", logFile, "-t", "5"}},
      {"play [周期截图] -screenshot D:/avox_shots -shot-interval 1000 -t 5",
       {"play", "-i", kPlayFile, "-offscreen", "-screenshot", "D:/avox_shots",
        "-shot-interval", "1000", "-t", "5"}},
      {"play [单次截图] -shot-at 3000 -screenshot D:/avox_shots -t 6",
       {"play", "-i", kPlayFile, "-offscreen", "-screenshot", "D:/avox_shots",
        "-shot-at", "3000", "-t", "6"}},
  };

  // 交互式: 列出用例让用户选一个执行, 执行完回到菜单, 直到选择退出
  while (true) {
    std::cout << "\n============================================" << std::endl;
    std::cout << "可用测试用例:" << std::endl;
    for (size_t i = 0; i < cases.size(); i++) {
      std::cout << "  " << (i + 1) << ". " << cases[i].title << std::endl;
    }
    std::cout << "  a. 全部执行" << std::endl;
    std::cout << "  0. 退出" << std::endl;
    std::cout << "请输入编号 (1-" << cases.size() << " / a / 0): ";
    std::string line;
    if (!std::getline(std::cin, line)) break;  // 输入结束 (如管道关闭)
    // 去首尾空白与回车
    while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) {
      line.erase(0, 1);
    }
    while (!line.empty() &&
           (line.back() == ' ' || line.back() == '\t' || line.back() == '\r')) {
      line.pop_back();
    }
    if (line.empty() || line == "0" || line == "q" || line == "quit" ||
        line == "exit") {
      break;
    }
    if (line == "a" || line == "all") {
      for (const auto& c : cases) runCase(c.title, c.args);
      continue;
    }
    try {
      int idx = std::stoi(line);
      if (idx >= 1 && idx <= static_cast<int>(cases.size())) {
        runCase(cases[idx - 1].title, cases[idx - 1].args);
      } else {
        std::cout << "无效编号: " << idx << std::endl;
      }
    } catch (...) {
      std::cout << "无法识别的输入: " << line << std::endl;
    }
  }
  std::cout << "再见!" << std::endl;
  return 0;
}
