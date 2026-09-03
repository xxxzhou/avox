#pragma once
#include <functional>
#include <string>
#include <vector>

#include "ArgParser.hpp"

namespace avox {

struct Command {
  std::string name;
  std::string desc;
  ArgParser parser;
  std::function<int(const ParsedArgs&)> run;
};

class CmdRegistry {
 public:
  CmdRegistry() = default;
  ~CmdRegistry() = default;

 private:
  std::vector<Command> commands;

 public:
  void add(Command cmd);
  const Command* find(const std::string& name) const;
  // 遍历所有命令 (供 helpText/Shell 枚举用)
  const std::vector<Command>& all() const { return commands; }
  std::string helpText() const;
  int execute(int argc, const char* const* argv) const;
};

// 内置命令注册中心 (Meyers 单例): 首次调用装入所有子命令。
// cmdExecute (cli 独立进程) 与 cmdExecuteLine (嵌入宿主) 共用同一份。
CmdRegistry& builtinCmdRegistry();

}
