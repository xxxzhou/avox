#include "CmdRegistry.hpp"

#include <cstdio>

#include "avox/AvoxVersion.h"
#include "avox_cmd/commands/CmdDevice.h"
#include "avox_cmd/commands/CmdPlay.h"
#include "avox_cmd/commands/CmdRecord.h"
#include "avox_cmd/commands/CmdInput.hpp"
#include "avox_cmd/commands/CmdOps.h"
#include "avox_cmd/commands/CmdPython.h"
#include "avox_cmd/commands/CmdVision.h"
#include "avox_cmd/commands/CmdAssets.hpp"
#include "avox_cmd/commands/CmdVoice.hpp"

namespace avox {

// CmdRegistry

void CmdRegistry::add(Command cmd) { commands.push_back(std::move(cmd)); }

const Command* CmdRegistry::find(const std::string& name) const {
  for (const auto& cmd : commands) {
    if (cmd.name == name) return &cmd;
  }
  return nullptr;
}

// 计算命令的最短无歧义前缀: 当前命令集中能唯一标识该命令的最短前缀。
// 用于帮助文本标注快捷缩写 (如 device -> [d]); 全名即唯一时返回全名。
static std::string minUniquePrefix(const std::vector<Command>& cmds, size_t idx) {
  const std::string& name = cmds[idx].name;
  for (size_t len = 1; len <= name.size(); ++len) {
    bool unique = true;
    for (size_t j = 0; j < cmds.size(); ++j) {
      if (j == idx) continue;
      const std::string& other = cmds[j].name;
      if (other.size() >= len && other.compare(0, len, name, 0, len) == 0) {
        unique = false;
        break;
      }
    }
    if (unique) return name.substr(0, len);
  }
  return name;
}

std::string CmdRegistry::helpText() const {
  std::string text;
  text += "avox_cli - avox command line tool (v" +
          std::string(AVOX_COMMIT_VERSION) + ")\n\n";
  text += "Usage: avox_cli <command> [options]\n\n";
  if (!commands.empty()) {
    text += "Commands:\n";
    // 展示名 = 命令名 + 最短无歧义前缀标注 (如 "device [d]"), 按展示名对齐
    std::vector<std::string> displayNames;
    displayNames.reserve(commands.size());
    size_t maxNameWidth = 0;
    for (size_t i = 0; i < commands.size(); ++i) {
      std::string display = commands[i].name;
      std::string prefix = minUniquePrefix(commands, i);
      if (prefix.size() < commands[i].name.size()) {
        display += " [" + prefix + "]";
      }
      maxNameWidth = std::max(maxNameWidth, display.size());
      displayNames.push_back(std::move(display));
    }
    for (size_t i = 0; i < commands.size(); ++i) {
      text += "  " + displayNames[i];
      text += std::string(maxNameWidth - displayNames[i].size() + 2, ' ');
      text += commands[i].desc + "\n";
    }
    text += "命令名可用无歧义前缀缩写 (方括号即最短缩写, 如 p=play)。\n";
  }
  text += "\nGlobal options:\n";
  text +=
      "  -loglevel <level>   日志级别: "
      "quiet/error/warning/info/verbose/debug\n";
  text += "  -version            显示版本\n";
  text += "  -help               显示帮助\n\n";
  text += "Run 'avox_cli <command> -help' for command details.\n";
  return text;
}

int CmdRegistry::execute(int argc, const char* const* argv) const {
  if (argc < 1) {
    fprintf(stderr, "%s", helpText().c_str());
    return 1;
  }
  std::string cmdName = argv[0];
  // 解析子命令: 精确匹配优先, 否则无歧义前缀匹配 (p->play, rec->record)
  const Command* cmd = find(cmdName);
  if (!cmd) {
    // 收集所有以 cmdName 为前缀的候选
    std::vector<const Command*> matches;
    for (const auto& c : commands) {
      if (c.name.size() >= cmdName.size() &&
          c.name.compare(0, cmdName.size(), cmdName) == 0) {
        matches.push_back(&c);
      }
    }
    if (matches.size() == 1) {
      cmd = matches[0];
    } else if (matches.size() > 1) {
      fprintf(stderr, "Ambiguous command '%s', matches:", cmdName.c_str());
      for (const auto* c : matches) fprintf(stderr, " %s", c->name.c_str());
      fprintf(stderr, "\nRun 'avox_cli -help' for usage.\n");
      return 1;
    } else {
      fprintf(stderr, "Unknown command: %s\n", cmdName.c_str());
      fprintf(stderr, "Run 'avox_cli -help' for usage.\n");
      return 1;
    }
  }
  // 解析子命令参数 (argv[1..argc-1])
  auto parsedArgs = cmd->parser.parse(argc - 1, argv + 1);
  // 如果传了 -help，显示子命令帮助
  if (parsedArgs.has("help")) {
    printf("%s", cmd->parser.helpText(cmdName).c_str());
    return 0;
  }
  // 执行子命令
  return cmd->run(parsedArgs);
}

CmdRegistry& builtinCmdRegistry() {
  static CmdRegistry reg;
  static bool inited = false;
  if (!inited) {
    reg.add(cmdDevice());
    reg.add(cmdPlay());
    reg.add(cmdRecord());
    reg.add(cmdInput());
    reg.add(cmdOps());
    reg.add(cmdPython());
    reg.add(cmdVision());
    reg.add(cmdAssets());
    reg.add(cmdVoice());
    inited = true;
  }
  return reg;
}

}
