#include "SubprocessRunner.hpp"

#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <cstdio>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace avox {

// ── 解析 python 命令 ──

void SubprocessRunner::resolvePython() {
  if (!pyExe_.empty()) return;  // 已解析

  // 1) 环境变量 AVOX_PYTHON (可含参数, 如 "py -3.13" 或 "/usr/bin/python3.12")
  std::string env;
#ifdef _WIN32
  char envBuf[1024];
  DWORD n = GetEnvironmentVariableA("AVOX_PYTHON", envBuf, sizeof(envBuf));
  if (n > 0 && n < sizeof(envBuf)) env = envBuf;
#else
  const char* envPtr = getenv("AVOX_PYTHON");
  if (envPtr && *envPtr) env = envPtr;
#endif
  if (!env.empty()) {
    // 按空格拆: 首词=可执行, 其余=前置参数
    size_t pos = env.find_first_of(" \t");
    if (pos == std::string::npos) {
      pyExe_ = env;
    } else {
      pyExe_ = env.substr(0, pos);
      size_t beg = pos;
      while (beg < env.size()) {
        beg = env.find_first_not_of(" \t", beg);
        if (beg == std::string::npos) break;
        size_t end = env.find_first_of(" \t", beg);
        if (end == std::string::npos) end = env.size();
        pyPreArgs_.push_back(env.substr(beg, end - beg));
        beg = end;
      }
    }
    return;
  }

  // 2) PATH 上 python (Win) / python3 (POSIX)
#ifdef _WIN32
  char found[512];
  if (SearchPathA(nullptr, "python.exe", nullptr, sizeof(found), found, nullptr) > 0) {
    pyExe_ = found;
    return;
  }
  if (SearchPathA(nullptr, "python3.exe", nullptr, sizeof(found), found, nullptr) > 0) {
    pyExe_ = found;
    return;
  }
#else
  auto which = [](const char* cmd) -> std::string {
    std::string full = std::string("which ") + cmd + " 2>/dev/null";
    FILE* fp = popen(full.c_str(), "r");
    if (!fp) return "";
    char buf[512] = {};
    if (!fgets(buf, sizeof(buf), fp)) { pclose(fp); return ""; }
    pclose(fp);
    size_t len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) buf[--len] = '\0';
    return len > 0 ? std::string(buf) : "";
  };
  std::string p = which("python3");
  if (p.empty()) p = which("python");
  if (!p.empty()) { pyExe_ = p; return; }
#endif
}

// ── available: 缓存探测结果 ──

bool SubprocessRunner::available() {
  if (availCache >= 0) return availCache != 0;
  resolvePython();
  if (pyExe_.empty()) { availCache = 0; return false; }
  // 试执行 python -c "print(1)" 确认可用
  std::string out = spawn({"-c", "print(1)"}, false);
  bool ok = (out.find("1") != std::string::npos);
  availCache = ok ? 1 : 0;
  return ok;
}

// ── runCode: python -c "<code>" ──

const char* SubprocessRunner::runCode(const char* code, const char* input) {
  lastResult.clear();
  if (!code || !*code) { lastResult = "FAIL: empty code"; return lastResult.c_str(); }
  // 传 input 经环境变量 AVOX_PY_INPUT (内联代码可 os.environ.get('AVOX_PY_INPUT'))
  if (input && *input) {
#ifdef _WIN32
    SetEnvironmentVariableA("AVOX_PY_INPUT", input);
#else
    setenv("AVOX_PY_INPUT", input, 1);
#endif
  }
  bool stderrPass = getenv("AVOX_PY_STDERR_PASSTHROUGH") != nullptr;
  lastResult = spawn({"-c", code}, stderrPass);
  if (input && *input) {
#ifdef _WIN32
    SetEnvironmentVariableA("AVOX_PY_INPUT", nullptr);
#else
    unsetenv("AVOX_PY_INPUT");
#endif
  }
  return lastResult.c_str();
}

// ── runScript: python <path> ──

const char* SubprocessRunner::runScript(const char* scriptPath, const char* input) {
  lastResult.clear();
  if (!scriptPath || !*scriptPath) { lastResult = "FAIL: empty: script path"; return lastResult.c_str(); }
  std::vector<std::string> args = {scriptPath};
  if (input && *input) args.push_back(input);
  bool stderrPass = getenv("AVOX_PY_STDERR_PASSTHROUGH") != nullptr;
  lastResult = spawn(args, stderrPass);
  return lastResult.c_str();
}

// ── runChain: python <path> <input> (脚本有 run() 自己调) ──

const char* SubprocessRunner::runChain(const char* scriptPath, const char* input) {
  return runScript(scriptPath, input);
}

// ── spawn: 核心子进程执行 ──

std::string SubprocessRunner::spawn(const std::vector<std::string>& args, bool stderrPassthrough) {
  resolvePython();
  if (pyExe_.empty()) return "FAIL: no python found on PATH (set AVOX_PYTHON env)";

  std::string result;

#ifdef _WIN32
  // ── Windows: CreateProcessA + 管道 ──
  // 拼命令行: "python.exe" [pyPreArgs...] [args...]
  auto quote = [](const std::string& s) -> std::string {
    if (s.find_first_of(" \t\"") == std::string::npos) return s;
    std::string q = "\"";
    for (char c : s) {
      if (c == '"') q += "\\\"";
      else q += c;
    }
    q += "\"";
    return q;
  };
  std::string cmdLine = quote(pyExe_);
  for (auto& a : pyPreArgs_) { cmdLine += " "; cmdLine += quote(a); }
  for (auto& a : args) { cmdLine += " "; cmdLine += quote(a); }

  // 管道: stdout (必捕获), stderr (捕获或直通)
  HANDLE hStdOutR = nullptr, hStdOutW = nullptr;
  SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
  CreatePipe(&hStdOutR, &hStdOutW, &sa, 0);
  SetHandleInformation(hStdOutR, HANDLE_FLAG_INHERIT, 0);

  HANDLE hStdErrW = stderrPassthrough ? GetStdHandle(STD_ERROR_HANDLE) : hStdOutW;

  STARTUPINFOA si = {};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  si.hStdOutput = hStdOutW;
  si.hStdError = hStdErrW;

  PROCESS_INFORMATION pi = {};
  BOOL ok = CreateProcessA(
      nullptr, const_cast<char*>(cmdLine.c_str()),
      nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi);

  CloseHandle(hStdOutW);  // 关写端 (子进程持有副本)

  if (!ok) {
    CloseHandle(hStdOutR);
    return "FAIL: CreateProcess (" + cmdLine + ")";
  }

  // 读 stdout
  char buf[4096];
  DWORD bytesRead = 0;
  while (ReadFile(hStdOutR, buf, sizeof(buf) - 1, &bytesRead, nullptr) && bytesRead > 0) {
    buf[bytesRead] = '\0';
    result.append(buf, bytesRead);
  }
  CloseHandle(hStdOutR);

  // 等进程结束 (超时 120s)
  DWORD wait = WaitForSingleObject(pi.hProcess, 120000);
  if (wait == WAIT_TIMEOUT) {
    TerminateProcess(pi.hProcess, 1);
    result = "FAIL: python subprocess timed out (120s)\n" + result;
  }
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);

#else
  // ── POSIX: pipe + fork + execvp ──
  int pipefd[2];
  if (pipe(pipefd) < 0) return "FAIL: pipe()";

  pid_t pid = fork();
  if (pid < 0) {
    close(pipefd[0]); close(pipefd[1]);
    return "FAIL: fork()";
  }

  if (pid == 0) {
    // 子进程
    close(pipefd[0]);
    dup2(pipefd[1], STDOUT_FILENO);
    if (!stderrPassthrough) dup2(pipefd[1], STDERR_FILENO);
    close(pipefd[1]);

    std::vector<char*> argv;
    argv.push_back(const_cast<char*>(pyExe_.c_str()));
    for (auto& a : pyPreArgs_) argv.push_back(const_cast<char*>(a.c_str()));
    for (auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    execvp(pyExe_.c_str(), argv.data());
    _exit(127);
  }

  // 父进程
  close(pipefd[1]);
  char buf[4096];
  ssize_t n;
  while ((n = read(pipefd[0], buf, sizeof(buf) - 1)) > 0) {
    buf[n] = '\0';
    result.append(buf, n);
  }
  close(pipefd[0]);

  int status = 0;
  waitpid(pid, &status, 0);
#endif

  return result;
}

}
