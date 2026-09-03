#pragma once
#include <string>
#include <vector>

#include "avox/AvoxDef.h"
#include "avox/module/Json.hpp"

namespace avox {

class CmdRegistry;

// 交互式 Shell: 无参数启动时进入命令循环。
class Shell {
 public:
  // 运行交互循环, registry 为借用(非拥有)。返回退出码。
  static int run(CmdRegistry& registry);
};

// 将一行文本分词为 argc/argv 风格的 token 列表。
// 支持双引号包裹含空格的路径 (如 "C:\my file.mp4")，支持 \" 转义。
std::vector<std::string> tokenizeLine(const std::string& line);

// 加载 cli 配置 (assets/config/cli.json): 缺失/空 -> 写默认值并返回。
// 结构 { "cmds":[{cmd,desc}...], "voice":{mode,prefix} };
// Shell 快捷命令菜单 (cmds) 与 voice 子命令 (voice) 共用同一份。
Json loadCliConfig();

}
