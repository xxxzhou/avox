#include "PwshTool.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "ToolArgs.hpp"
#include "ToolIo.hpp"
#include "avox/module/Json.hpp"
#include "avox/module/LogHelper.hpp"

namespace avox {

namespace {

// 单流内联上限 (保尾部): 超长输出截断, 全量前缀落 spill 文件并把路径报给模型
// (dsh #2300)。落盘失败降级为 "(unavailable)"。
constexpr size_t kMaxStreamBytes = 256 * 1024;
// 单流落盘上限 (dsh DEFAULT_MAX_SPILL_BYTES 同源): 溢出超过此长度的流, spill 文件
// 保前缀、内存保尾 —— 两段合起来不再全量, 但落盘有界。
constexpr uint64_t kMaxSpillBytes = 64ULL * 1024 * 1024;

// 单流收集器 (dsh CollectedOutput 同语义: text/truncated/spillPath)。
// 内存保尾; 首次溢出时把含头部的当前窗口冲刷进 spill 文件, 此后边收边追加。
// 落盘打不开/写失败 → 路径置空, 截断标注降级为 "(unavailable)"。
struct StreamCollector {
  std::string text;        // 尾部窗口 (≤ kMaxStreamBytes)
  bool truncated = false;  // 曾溢出 (哪怕一字节)
  std::string spillPath;   // 溢出后才有; 空 = 落盘不可用
  std::FILE* spill = nullptr;
  uint64_t spilled = 0;    // 已落盘字节
  const char* tag = "out";  // 文件名区分 out/err
  ~StreamCollector() {
    if (spill != nullptr) std::fclose(spill);
  }
  void append(const char* data, size_t len) {
    if (!truncated && text.size() + len <= kMaxStreamBytes) {
      text.append(data, len);
      return;
    }
    if (!truncated) {
      // 首次溢出: 先把带头部的前段冲进文件, 截断标记自此为真。
      truncated = true;
      openSpill();
      writeSpill(text.data(), text.size());
      text.clear();
    }
    writeSpill(data, len);
    text.append(data, len);
    if (text.size() > kMaxStreamBytes) {
      text.erase(0, text.size() - kMaxStreamBytes);
    }
  }
  void openSpill() {
    // 系统临时目录 + 一次性序号 (dsh 同用 tmpdir, OS 负责回收; 文件要留着给模型回头读)。
    static std::atomic<unsigned long long> counter{0};
    const unsigned long long seq = ++counter;
    std::error_code ec;
    std::filesystem::path dir = std::filesystem::temp_directory_path(ec);
    if (ec) return;
    std::filesystem::path file =
        dir / ("avox-pwsh-" + std::to_string(seq) + "-" + tag + ".log");
    std::FILE* handle = nullptr;
#ifdef _WIN32
    fopen_s(&handle, file.string().c_str(), "wb");
#else
    handle = std::fopen(file.string().c_str(), "wb");
#endif
    if (handle == nullptr) return;
    spill = handle;
    spillPath = file.string();
  }
  // 落盘失败即放弃 (半截文件标 "full output" 是撒谎): 关文件、清路径。
  void writeSpill(const char* data, size_t len) {
    if (spill == nullptr || spilled >= kMaxSpillBytes) return;
    const uint64_t room = kMaxSpillBytes - spilled;
    const size_t take = len > room ? static_cast<size_t>(room) : len;
    if (take > 0 && std::fwrite(data, 1, take, spill) != take) {
      std::fclose(spill);
      spill = nullptr;
      spillPath.clear();
      spilled = kMaxSpillBytes;  // 不再尝试
      return;
    }
    spilled += take;
  }
};
// timeoutMs 的缺省与上限 (ToolDefinition.timeoutMs 同步取上限, 超时策略兜底)。
constexpr int kDefaultTimeoutMs = 120000;
constexpr int kMaxTimeoutMs = 600000;

constexpr const char* kParameters = R"json({
    "type": "object",
    "properties": {
      "command": {"type": "string", "description": "要执行的 PowerShell 命令 (必填, 非空)"},
      "description": {"type": "string", "description": "这条命令做什么的一句简述 (必填, 非空)"},
      "timeoutMs": {"type": "integer", "description": "超时毫秒数 (正数; 默认 120000, 上限 600000, 超时杀进程树)"},
      "workdir": {"type": "string", "description": "工作目录 (相对路径按会话工作目录解析, 默认会话工作目录)"}
    },
    "required": ["command", "description"]
  })json";

#ifdef _WIN32

// UTF-8 → UTF-16LE → base64: PowerShell -EncodedCommand 只吃这个形态。
// 走 EncodedCommand 而非裸 -Command: 完全绕开命令行引号转义与中文编码问题。
std::string toBase64(const std::string& bytes) {
  static const char* kTable =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((bytes.size() + 2) / 3 * 4);
  for (size_t i = 0; i < bytes.size(); i += 3) {
    const uint32_t b0 = static_cast<uint8_t>(bytes[i]);
    const uint32_t b1 = i + 1 < bytes.size() ? static_cast<uint8_t>(bytes[i + 1]) : 0;
    const uint32_t b2 = i + 2 < bytes.size() ? static_cast<uint8_t>(bytes[i + 2]) : 0;
    const uint32_t triple = (b0 << 16) | (b1 << 8) | b2;
    out += kTable[(triple >> 18) & 0x3F];
    out += kTable[(triple >> 12) & 0x3F];
    out += i + 1 < bytes.size() ? kTable[(triple >> 6) & 0x3F] : '=';
    out += i + 2 < bytes.size() ? kTable[triple & 0x3F] : '=';
  }
  return out;
}

std::string toEncodedCommand(const std::string& utf8) {
  const int wideLength =
      MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()),
                          nullptr, 0);
  std::wstring wide(static_cast<size_t>(wideLength), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()),
                      &wide[0], wideLength);
  const char* bytes = reinterpret_cast<const char*>(wide.data());
  return toBase64(std::string(bytes, wide.size() * sizeof(wchar_t)));
}

// PATH 上找 pwsh.exe, 找不到退 powershell.exe (Windows PowerShell 5.1)。
std::string resolveShell() {
  char found[MAX_PATH];
  if (SearchPathA(nullptr, "pwsh.exe", nullptr, sizeof(found), found, nullptr) > 0) {
    return found;
  }
  if (SearchPathA(nullptr, "powershell.exe", nullptr, sizeof(found), found, nullptr)
      > 0) {
    return found;
  }
  return std::string();
}

// 杀整个进程树 (pwsh 可能 spawn 了子进程; TerminateProcess 只杀根)。
void killProcessTree(DWORD pid) {
  char command[64];
  snprintf(command, sizeof(command), "taskkill /T /F /PID %lu",
           static_cast<unsigned long>(pid));
  STARTUPINFOA si = {};
  PROCESS_INFORMATION pi = {};
  si.cb = sizeof(si);
  if (CreateProcessA(nullptr, command, nullptr, nullptr, FALSE,
                     CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
    WaitForSingleObject(pi.hProcess, 10000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
  }
}

// 读管道到 EOF。管道写端全关后 ReadFile 返回 0, 线程自然退出。
void pipeReader(HANDLE readEnd, StreamCollector* out) {
  char buffer[4096];
  DWORD bytesRead = 0;
  while (ReadFile(readEnd, buffer, sizeof(buffer), &bytesRead, nullptr)
         && bytesRead > 0) {
    out->append(buffer, bytesRead);
  }
}

struct PwshRun {
  bool spawned = false;
  bool timedOut = false;
  bool aborted = false;
  int exitCode = 0;
  std::string signal;  // Windows 恒空 (无信号概念)
  std::string stdoutText;
  std::string stderrText;
  bool stdoutTruncated = false;
  bool stderrTruncated = false;
  std::string stdoutSpill;
  std::string stderrSpill;
};

PwshRun runPwsh(const std::string& command, const std::string& workdir,
                int timeoutMs, const ToolExecution& exec) {
  PwshRun run;
  const std::string shell = resolveShell();
  if (shell.empty()) return run;

  // 命令行走 CreateProcessW (UTF-8→UTF-16), 中文路径/命令不怕代码页。
  std::string cmdLine = "\"" + shell + "\" -NoProfile -NonInteractive -EncodedCommand "
                        + toEncodedCommand(command);
  const int wideCmdLength =
      MultiByteToWideChar(CP_UTF8, 0, cmdLine.c_str(), static_cast<int>(cmdLine.size()),
                          nullptr, 0);
  std::wstring wideCmd(static_cast<size_t>(wideCmdLength), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, cmdLine.c_str(), static_cast<int>(cmdLine.size()),
                      &wideCmd[0], wideCmdLength);
  // workdir 同样转宽字符; currentDir 必须指向存活到 CreateProcessW 返回后的串。
  std::wstring wideWorkdir;
  if (!workdir.empty()) {
    const int wideDirLength = MultiByteToWideChar(
        CP_UTF8, 0, workdir.c_str(), static_cast<int>(workdir.size()), nullptr, 0);
    wideWorkdir.assign(static_cast<size_t>(wideDirLength), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, workdir.c_str(),
                        static_cast<int>(workdir.size()), &wideWorkdir[0],
                        wideDirLength);
  }
  const wchar_t* currentDir = wideWorkdir.empty() ? nullptr : wideWorkdir.c_str();

  SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
  HANDLE outRead = nullptr, outWrite = nullptr;
  HANDLE errRead = nullptr, errWrite = nullptr;
  CreatePipe(&outRead, &outWrite, &sa, 0);
  SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);
  CreatePipe(&errRead, &errWrite, &sa, 0);
  SetHandleInformation(errRead, HANDLE_FLAG_INHERIT, 0);
  // stdin 指向 NUL: -NonInteractive 不读, 但句柄缺失会让某些宿主报错。
  HANDLE nullInput = CreateFileA("NUL", GENERIC_READ,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                                 OPEN_EXISTING, 0, nullptr);

  STARTUPINFOW si = {};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = nullInput;
  si.hStdOutput = outWrite;
  si.hStdError = errWrite;
  PROCESS_INFORMATION pi = {};
  const BOOL ok = CreateProcessW(nullptr, &wideCmd[0], nullptr, nullptr, TRUE, 0,
                                 nullptr, currentDir, &si, &pi);
  // 父进程侧写端立即关闭: 子进程退出后读端才会见到 EOF。
  CloseHandle(outWrite);
  CloseHandle(errWrite);
  if (nullInput != INVALID_HANDLE_VALUE) CloseHandle(nullInput);
  if (!ok) {
    CloseHandle(outRead);
    CloseHandle(errRead);
    return run;
  }
  run.spawned = true;

  // 两条读线程并行收流, 防止一管塞满导致子进程写阻塞死锁。
  StreamCollector outCol;
  outCol.tag = "out";
  StreamCollector errCol;
  errCol.tag = "err";
  std::thread outThread(pipeReader, outRead, &outCol);
  std::thread errThread(pipeReader, errRead, &errCol);

  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  bool finished = false;
  while (!finished) {
    const DWORD wait = WaitForSingleObject(pi.hProcess, 20);
    if (wait == WAIT_OBJECT_0) {
      finished = true;
      break;
    }
    if (exec.signal != nullptr && exec.signal->aborted()) {
      killProcessTree(pi.dwProcessId);
      run.aborted = true;
      break;
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      killProcessTree(pi.dwProcessId);
      run.timedOut = true;
      break;
    }
  }
  if (finished) {
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    run.exitCode = static_cast<int>(code);
  }
  // 等读线程排干 (进程树死透后写端全关, ReadFile 很快返回 0)。
  outThread.join();
  errThread.join();
  CloseHandle(outRead);
  CloseHandle(errRead);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  run.stdoutText = std::move(outCol.text);
  run.stderrText = std::move(errCol.text);
  run.stdoutTruncated = outCol.truncated;
  run.stderrTruncated = errCol.truncated;
  run.stdoutSpill = std::move(outCol.spillPath);
  run.stderrSpill = std::move(errCol.spillPath);
  return run;
}

#else  // POSIX

struct PwshRun {
  bool spawned = false;
  bool timedOut = false;
  bool aborted = false;
  int exitCode = 0;
  std::string signal;
  std::string stdoutText;
  std::string stderrText;
  bool stdoutTruncated = false;
  bool stderrTruncated = false;
  std::string stdoutSpill;
  std::string stderrSpill;
};

void fdReader(int fd, StreamCollector* out) {
  char buffer[4096];
  ssize_t n = 0;
  while ((n = read(fd, buffer, sizeof(buffer))) > 0) {
    out->append(buffer, static_cast<size_t>(n));
  }
}

PwshRun runPwsh(const std::string& command, const std::string& workdir,
                int timeoutMs, const ToolExecution& exec) {
  PwshRun run;
  // POSIX 部署没有 pwsh 时退 /bin/sh (工具名与 schema 保持 dsh 的 pwsh 契约)。
  const char* shell = "/bin/sh";
  int outPipe[2], errPipe[2];
  if (pipe(outPipe) != 0 || pipe(errPipe) != 0) return run;
  const pid_t pid = fork();
  if (pid < 0) {
    close(outPipe[0]); close(outPipe[1]);
    close(errPipe[0]); close(errPipe[1]);
    return run;
  }
  if (pid == 0) {
    // 子进程: 独立进程组 (超时可整组杀), 输出接管道。
    setpgid(0, 0);
    dup2(outPipe[1], STDOUT_FILENO);
    dup2(errPipe[1], STDERR_FILENO);
    close(outPipe[0]); close(outPipe[1]);
    close(errPipe[0]); close(errPipe[1]);
    if (!workdir.empty()) chdir(workdir.c_str());
    execl(shell, "sh", "-c", command.c_str(), static_cast<char*>(nullptr));
    _exit(127);
  }
  run.spawned = true;
  close(outPipe[1]);
  close(errPipe[1]);
  StreamCollector outCol;
  outCol.tag = "out";
  StreamCollector errCol;
  errCol.tag = "err";
  std::thread outThread(fdReader, outPipe[0], &outCol);
  std::thread errThread(fdReader, errPipe[0], &errCol);

  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  bool finished = false;
  while (!finished) {
    int status = 0;
    const pid_t reaped = waitpid(pid, &status, WNOHANG);
    if (reaped == pid) {
      finished = true;
      if (WIFSIGNALED(status)) {
        run.signal = WTERMSIG(status) == SIGKILL ? "SIGKILL"
                     : WTERMSIG(status) == SIGTERM ? "SIGTERM"
                     : std::to_string(WTERMSIG(status));
      } else {
        run.exitCode = WEXITSTATUS(status);
      }
      break;
    }
    if (exec.signal != nullptr && exec.signal->aborted()) {
      kill(-pid, SIGKILL);
      kill(pid, SIGKILL);
      run.aborted = true;
      break;
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      kill(-pid, SIGKILL);
      kill(pid, SIGKILL);
      run.timedOut = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  if (run.timedOut || run.aborted) {
    int status = 0;
    waitpid(pid, &status, 0);
  }
  outThread.join();
  errThread.join();
  close(outPipe[0]);
  close(errPipe[0]);
  run.stdoutText = std::move(outCol.text);
  run.stderrText = std::move(errCol.text);
  run.stdoutTruncated = outCol.truncated;
  run.stderrTruncated = errCol.truncated;
  run.stdoutSpill = std::move(outCol.spillPath);
  run.stderrSpill = std::move(errCol.spillPath);
  return run;
}

#endif  // _WIN32

// dsh streamText: 截断流尾标注 + 全量输出的落盘路径 (dsh #2300)。落盘不可用时
// 固定为 "(unavailable)"。
std::string streamText(const std::string& text, bool truncated,
                       const std::string& spillPath) {
  if (!truncated) return text;
  return text + "\n[output truncated; full output: "
         + (spillPath.empty() ? std::string("(unavailable)") : spillPath) + "]";
}

// dsh renderPwshResult (无沙箱支): 正文 → [stderr] 段 → 标记行。
// 非零退出码/超时是「被报告的事实」而非工具失败 —— 模型自己决定怎么反应。
std::string renderPwsh(const PwshRun& run, int timeoutMs) {
  std::string body =
      streamText(run.stdoutText, run.stdoutTruncated, run.stdoutSpill);
  const std::string err =
      streamText(run.stderrText, run.stderrTruncated, run.stderrSpill);
  if (!err.empty()) {
    // 段间单换行 (stdout 通常已带换行结尾)。
    if (!body.empty() && body.back() != '\n') body += '\n';
    body += "[stderr]\n" + err;
  }
  if (body.empty()) body = "(no output)";
  std::vector<std::string> markers;
  if (run.timedOut) markers.push_back("[timed out after " + std::to_string(timeoutMs) + "ms]");
  if (!run.signal.empty()) {
    markers.push_back("[killed by signal: " + run.signal + "]");
  } else if (run.exitCode != 0) {
    markers.push_back("[exit code: " + std::to_string(run.exitCode) + "]");
  }
  if (markers.empty()) return body;
  if (body.back() != '\n') body += '\n';
  for (size_t i = 0; i < markers.size(); ++i) {
    body += markers[i];
    if (i + 1 < markers.size()) body += '\n';
  }
  body += '\n';
  return body;
}

ToolResult executePwsh(const ToolExecution& exec) {
  LOGFLF(LogLevel::info, "agent tool pwsh");
  const Json args = parseToolArgs(exec.argumentsJson);
  const std::string command = toolArgString(args, "command");
  const std::string description = toolArgString(args, "description");
  int timeoutMs = toolArgInt(args, "timeoutMs", kDefaultTimeoutMs);
  const std::string workdirArg = toolArgString(args, "workdir");

  if (command.empty()) {
    return toolError(ToolOutcome::Fatal, "invalid command: expected a non-empty string",
                     TOOL_CODE_INVALID_ARGS);
  }
  if (description.empty()) {
    return toolError(ToolOutcome::Fatal,
                     "invalid description: expected a non-empty string",
                     TOOL_CODE_INVALID_ARGS);
  }
  if (args.bObject() && args.find("timeoutMs")
      && (!args["timeoutMs"].bInt() || timeoutMs <= 0)) {
    return toolError(ToolOutcome::Fatal,
                     "invalid timeoutMs: expected a positive number, got "
                         + args["timeoutMs"].dump(),
                     TOOL_CODE_INVALID_ARGS);
  }
  if (timeoutMs > kMaxTimeoutMs) timeoutMs = kMaxTimeoutMs;
  std::string workdir;
  if (!workdirArg.empty()) {
    const std::filesystem::path resolved = resolveAgentPath(workdirArg);
    std::error_code ec;
    if (!std::filesystem::is_directory(resolved, ec)) {
      return toolError(ToolOutcome::Fatal, "工作目录不存在: " + resolved.string(),
                       TOOL_CODE_INVALID_ARGS);
    }
    workdir = resolved.string();
  }

  PwshRun run = runPwsh(command, workdir, timeoutMs, exec);
  if (!run.spawned) {
    return toolError(ToolOutcome::Fatal,
                     "无法启动 PowerShell (PATH 上没有 pwsh.exe / powershell.exe)",
                     "SHELL_NOT_FOUND");
  }
  if (run.aborted) {
    return toolError(ToolOutcome::Aborted, "pwsh 执行被取消: " + command,
                     TOOL_CODE_ABORTED);
  }
  ToolResult result = toolOk(renderPwsh(run, timeoutMs));
  Json meta(Json::JsonObject{});
  meta["exitCode"] = static_cast<int64_t>(run.exitCode);
  meta["timedOut"] = run.timedOut;
  // 截断时的全量落盘路径 (dsh spillPath): 展示层与模型文本同源。
  if (run.stdoutTruncated && !run.stdoutSpill.empty()) {
    meta["stdoutSpillPath"] = run.stdoutSpill;
  }
  if (run.stderrTruncated && !run.stderrSpill.empty()) {
    meta["stderrSpillPath"] = run.stderrSpill;
  }
  result.meta = meta.dump();
  return result;
}

std::optional<ToolCallView> presentPwsh(const std::string& argumentsJson) {
  const Json args = parseToolArgs(argumentsJson);
  const std::string command = toolArgString(args, "command");
  const std::string description = toolArgString(args, "description");
  const std::string workdir = toolArgString(args, "workdir");
  if (command.empty()) return std::nullopt;
  TerminalCallCard card;
  // 标题用模型自述意图 (description), 与 run_code 的卡片一致。
  card.command = description.empty() ? command : description;
  if (!workdir.empty()) card.cwd = workdir;
  return ToolCallView{card};
}

std::optional<ToolResultView> presentPwshResult(const std::string& argumentsJson,
                                                const ToolResult& result) {
  TerminalResultCard card;
  std::string text;
  if (!result.content.empty()) {
    if (const auto* block = std::get_if<TextBlock>(&result.content[0])) {
      text = block->text;
    }
  }
  card.output = std::move(text);
  const Json meta = result.meta.has_value() ? parseToolArgs(*result.meta) : Json();
  if (meta.bObject() && meta.find("exitCode") && meta["exitCode"].bInt()) {
    card.exitCode = static_cast<int>(meta["exitCode"].get<int64_t>());
  }
  return ToolResultView{card};
}

}  // namespace

ToolDefinition makePwshTool() {
  ToolDefinition definition;
  definition.name = "pwsh";
  definition.description =
      "执行 PowerShell 命令, 捕获 stdout 与 stderr。非零退出码以 [exit code: N] 标记"
      "返回 (不算工具失败); 超时杀整个进程树并标 [timed out after Nms]。"
      "超长输出截尾返回, 全量输出落盘并在标注里给出文件路径。"
      "timeoutMs 默认 120000 上限 600000; workdir 默认会话工作目录。"
      "必须带 description 一句说明意图。";
  definition.parametersJson = kParameters;
  definition.execute = executePwsh;
  definition.timeoutMs = kMaxTimeoutMs;
  definition.presentCall = presentPwsh;
  definition.presentResult = presentPwshResult;
  // executionMode 不设 = 独占 (dsh pwsh 未声明 isConcurrencySafe)。
  return definition;
}

}
