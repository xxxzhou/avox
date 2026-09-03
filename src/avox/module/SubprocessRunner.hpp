#pragma once

#include <string>
#include <vector>

#include "../AvoxBase.h"

namespace avox {

// IPyRunner 的子进程实现: spawn 机器 Python (不再嵌入 CPython, 不绑定版本)。
// runCode  → python -c "<code>"
// runScript → python <path>
// runChain  → python <path> <input>
// 均经管道收 stdout+stderr, 返回缓冲 (下次调失效, 同 PyRunner 约定)。
class SubprocessRunner : public IPyRunner {
 public:
  bool available() override;
  const char* runChain(const char* scriptPath, const char* input) override;
  const char* runCode(const char* code, const char* input) override;
  const char* runScript(const char* scriptPath, const char* input) override;

 private:
  // 解析 python 命令: 优先 AVOX_PYTHON 环境变量, 否则 PATH 上 python (Win) / python3 (POSIX)
  // AVOX_PYTHON 可含参数 (如 "py -3.13"), 拆为 pyExe_ + pyPreArgs_
  void resolvePython();
  // 核心: spawn python 进程, 传 args, 收 stdout (可选 stderr 直通)
  std::string spawn(const std::vector<std::string>& args, bool stderrPassthrough);
  // 返回缓冲 (同 PyRunner 约定: 内部持有, 下次调失效)
  std::string lastResult;
  // 解析后的 python 可执行 (如 "py.exe" 或 "D:/Python313/python.exe")
  std::string pyExe_;
  // AVOX_PYTHON 中可执行之后的参数 (如 ["-3.13"]), 每次调 spawn 时前插到 args
  std::vector<std::string> pyPreArgs_;
  // 缓存 available 结果 (首次探测, 之后复用)
  int availCache = -1;  // -1=未探测, 0=false, 1=true
};

}
