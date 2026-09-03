#include "RunCodeTool.hpp"

#include <string>

#include "ToolArgs.hpp"
#include "avox/AvoxBase.h"  // getPyRunner (runCode / runScript)
#include "avox/module/LogHelper.hpp"

namespace avox {

namespace {

// 对齐 dsh 的 run_code 契约: code 与 description 必填; script/input 为 avox 扩展 (跑 .py)。
constexpr const char* kParameters = R"json({
    "type": "object",
    "properties": {
      "code": {"type": "string", "description": "内联 python 代码 (直接 exec; 必填)。可用 import avox 调截图/OCR/输入/播放"},
      "description": {"type": "string", "description": "这段代码做什么的一句简述 (必填, UI 展示)"},
      "script": {"type": "string", "description": ".py 脚本路径 (importlib 加载; 有 run() 则调 run(input)); 指定时优先于 code"},
      "input": {"type": "string", "description": "传给脚本 run(input), 或内联代码的 input 变量"}
    },
    "required": ["code", "description"]
  })json";

ToolResult executeRunCode(const ToolExecution& exec) {
  LOGFLF(LogLevel::info, "agent tool run_code");
  const Json args = parseToolArgs(exec.argumentsJson);
  const std::string code = toolArgString(args, "code");
  const std::string description = toolArgString(args, "description");
  const std::string script = toolArgString(args, "script");
  const std::string input = toolArgString(args, "input");

  if (description.empty()) {
    return toolError(ToolOutcome::Fatal,
                     "run_code 缺少必填参数 description (这段代码做什么的一句简述)。",
                     TOOL_CODE_INVALID_ARGS);
  }
  if (code.empty() && script.empty()) {
    return toolError(ToolOutcome::Fatal,
                     "run_code 缺少必填参数 code (内联 python 代码) 或 script (.py 路径), 二选一。",
                     TOOL_CODE_INVALID_ARGS);
  }

  IPyRunner* runner = getPyRunner();
  if (runner == nullptr) {
    return toolError(ToolOutcome::Fatal, "机器上没有找到 python", "PYTHON_NOT_FOUND");
  }

  std::string output;
  if (!script.empty()) {
    output = runner->runScript(script.c_str(), input.c_str());
  } else {
    output = runner->runCode(code.c_str(), input.c_str());
  }

  // 子进程的失败约定: 输出以 "FAIL:" 开头。
  //
  // 这个字符串前缀判定留在**这一层**是对的 —— 它是与 python runner 之间的约定, 属于工具
  // 内部实现。它不再泄漏到框架: 框架看的是本函数返回的 outcome。
  if (output.rfind("FAIL:", 0) == 0) {
    ToolResult result;
    result.outcome = ToolOutcome::Fatal;
    result.code = "PYTHON_FAILED";
    result.content.push_back(TextBlock{std::move(output)});
    return result;
  }
  return toolOk(std::move(output));
}

std::optional<ToolCallView> presentRunCode(const std::string& argumentsJson) {
  const Json args = parseToolArgs(argumentsJson);
  const std::string script = toolArgString(args, "script");
  const std::string code = toolArgString(args, "code");
  const std::string description = toolArgString(args, "description");

  TerminalCallCard card;
  // 与 dsh 一致: 标题用 description (模型自述的调用意图); 无 description 时回退到代码。
  if (!description.empty()) {
    card.command = description;
  } else if (!script.empty()) {
    card.command = "python " + script;
  } else if (!code.empty()) {
    // 内联代码取首行作标题: 完整代码在原始入参里, UI 想看可以展开。
    const size_t lineEnd = code.find('\n');
    card.command = "python -c " + (lineEnd == std::string::npos
                                      ? code
                                      : code.substr(0, lineEnd) + " …");
  } else {
    return std::nullopt;
  }
  return ToolCallView{card};
}

}  // namespace

ToolDefinition makeRunCodeTool() {
  ToolDefinition definition;
  definition.name = "run_code";
  definition.description =
      "执行 python 代码或脚本 (可用 import avox 调 avox 能力: "
      "截图/OCR/输入/播放/cmdExecuteLine 等)。code 传内联代码, 或 script 传 .py 路径; "
      "必须带 description 说明这段代码在做什么。返回 stdout+stderr。";
  definition.parametersJson = kParameters;
  definition.execute = executeRunCode;
  // 预算最长: 采集类脚本本来就慢。注意旧工具没有 signal, 所以超时只能中断等待 ——
  // 子进程会跑到自己结束。真正的协作式取消要等 runner 支持传入取消句柄。
  definition.timeoutMs = 300000;
  definition.presentCall = presentRunCode;
  // executionMode 不设 = 独占 (默认)。
  return definition;
}

}