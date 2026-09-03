/**
 * @file CmdPython.cpp
 * @brief python 子命令 - spawn 机器 python 跑代码/脚本 (avox 绑定可用)
 *
 * SubprocessRunner (编进 avox.dll) 支持两种执行:
 *   - python <脚本.py> [-i 输入]: runScript, importlib 按路径加载; 有 run() 则调 run(input)
 *   - python -e "<内联代码>" [-i 输入]: runCode, exec 内联代码 (input 变量可用)
 * 均捕获 stdout+stderr (+ run 返回值 / 异常 traceback)。
 * 经 AvoxBase.h::getPyRunner() 拿 IPyRunner 单例 (SubprocessRunner; python 不可用时降级)。
 *
 * -e 特殊处理: 内联代码常含空格/引号 (如 print('hello')), tokenizeLine 按空格分词
 * 会把代码切碎。故 -e 取值后, 把剩余 positional args 用空格拼回, 还原完整代码。
 * 用户也可用双引号包裹: python -e "print('hello')" (双引号被 tokenizeLine 剥掉)。
 */

#include "CmdPython.h"

#include <cstdio>
#include <string>

#include "avox/AvoxBase.h"   // getPyRunner (runCode / runScript)

namespace avox {

Command cmdPython() {
  Command cmd;
  cmd.name = "python";
  cmd.desc = "spawn 机器 python 跑脚本: <脚本.py> 或 -e <代码> (可 import avox)";
  // 内联代码 (-e / --exec): 与位置参数(脚本路径)二选一
  cmd.parser.addArg({"-e", "--exec", ArgType::String, false,
                     "内联 python 代码 (直接 exec; input 变量可用)", ""});
  // input: 传给脚本 run(input) 或内联代码 input 变量
  cmd.parser.addArg({"-i", "--input", ArgType::String, false,
                     "传给脚本 run(input), 或内联代码的 input 变量", ""});
  cmd.run = [](const ParsedArgs& args) -> int {
    std::string code = args.getString("exec");
    std::string input = args.getString("input");
    const std::vector<std::string>& pos = args.positional();
    IPyRunner* py = getPyRunner();
    if (!py) {
      fprintf(stderr, "python 命令需要机器上安装 python (PATH 上找不到 python)\n");
      return 1;
    }
    std::string out;
    if (!code.empty()) {
      // -e 后的代码可能被 tokenizeLine 按空格切碎成多个 positional,
      // 把它们用空格拼回, 还原用户输入的完整内联代码。
      // 例: python -e print('hello')  → code="print('hello", pos=["from","embedded","python')"]
      //     拼回 → "print('hello from embedded python')"
      if (!pos.empty()) {
        for (const auto& p : pos) {
          code += " ";
          code += p;
        }
      }
      out = py->runCode(code.c_str(), input.c_str());
    } else if (!pos.empty()) {
      out = py->runScript(pos[0].c_str(), input.c_str());
    } else {
      fprintf(stderr,
              "用法: avox_cli python <脚本.py> [-i 输入]  或  avox_cli python -e \"<内联代码>\" [-i 输入]\n");
      return 1;
    }
    if (out.rfind("FAIL:", 0) == 0) {
      fprintf(stderr, "%s\n", out.c_str());
      return 1;
    }
    printf("%s", out.c_str());
    if (!out.empty() && out.back() != '\n') printf("\n");
    return 0;
  };
  return cmd;
}

}
