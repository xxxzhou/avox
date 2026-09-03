#include "ReadTool.hpp"

#include <fstream>
#include <string>

#include "ToolArgs.hpp"
#include "avox/Avox.hpp"  // getAvoxPath / expandEnvPath
#include "avox/module/LogHelper.hpp"

namespace avox {

namespace {

constexpr const char* kParameters = R"json({
    "type": "object",
    "properties": {
      "path": {"type": "string", "description": "文件路径; 仅文件名(无路径分隔符)默认取 <运行目录>/logs/"},
      "offset": {"type": "integer", "description": "起始行号(1-based, 默认1)"},
      "lines": {"type": "integer", "description": "读取行数(默认100, 最大500)"}
    },
    "required": ["path"]
  })json";

ToolResult executeRead(const ToolExecution& exec) {
  const Json args = parseToolArgs(exec.argumentsJson);
  std::string path = toolArgString(args, "path");
  // offset 为 1-based, 与 dsh 的 read 契约一致。
  int offset = toolArgInt(args, "offset", 1);
  int lines = toolArgInt(args, "lines", 100);
  if (offset < 1) offset = 1;
  if (lines > 500) lines = 500;
  if (lines < 1) lines = 1;

  // path 必填: 漏传时明确提示, 引导模型补 path 重读 (而非改去重新播放采集)。
  if (path.empty()) {
    return toolError(
        ToolOutcome::Fatal,
        "read 缺少 path 参数 (必填)。重读时补上 path: 文件名(如 "
        "play_20260709_104035.log, 默认取 <运行目录>/logs/) 或全路径。"
        "读日志失败不要改成重新播放 (限时链接一次性, 重播必失败)。",
        TOOL_CODE_INVALID_ARGS);
  }

  // 仅文件名 → <运行目录>/logs/<文件名>, 与 avox_cli 写日志目录一致。
  if (path.find('/') == std::string::npos && path.find('\\') == std::string::npos) {
    path = getAvoxPath() + "/logs/" + path;
  }
  path = expandEnvPath(path);   // 展开 %APPDATA% 等, 支持 hysp_pc 默认日志路径

  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return toolError(ToolOutcome::Fatal, "无法读取文件: " + path, "FILE_NOT_READABLE");
  }

  std::string result;
  std::string line;
  int lineNo = 0;        // 1-based 行号
  int readCount = 0;
  int totalLines = 0;
  while (std::getline(file, line)) {
    totalLines++;
    if (lineNo >= offset && readCount < lines) {
      result += std::to_string(lineNo) + ": " + line;
      result += '\n';
      readCount++;
    }
    lineNo++;
  }

  // 末尾标注位置, 供模型决定下一轮读哪里。
  result += "\n--- 行 " + std::to_string(offset) + "-"
            + std::to_string(offset + readCount - 1) + " / 共 "
            + std::to_string(totalLines) + " 行 ---";
  if (offset + readCount <= totalLines) {
    result += "\n(后续行: " + std::to_string(totalLines - offset - readCount + 1)
              + " 行未读, 用 read({path:\"" + path + "\", offset:"
              + std::to_string(offset + readCount) + ", lines:"
              + std::to_string(lines) + "}) 继续)";
  }
  return toolOk(std::move(result));
}

std::optional<ToolCallView> presentRead(const std::string& argumentsJson) {
  const Json args = parseToolArgs(argumentsJson);
  const std::string path = toolArgString(args, "path");
  if (path.empty()) return std::nullopt;

  GenericCallCard card;
  card.kind = ToolCallKind::Read;
  card.title = "读取 " + path;
  // offset 已为 1-based, 直接作为跳转行号。
  const int offset = toolArgInt(args, "offset", 1);
  card.locations.push_back(FileLocation{path, offset > 1 ? std::optional<int>(offset)
                                                         : std::nullopt});
  return ToolCallView{card};
}

}  // namespace

ToolDefinition makeReadTool() {
  ToolDefinition definition;
  definition.name = "read";
  definition.description =
      "读取大内容的指定行范围, 返回带行号的内容, 用于分段分析。每轮读取后根据发现决定下一轮读哪里。";
  definition.parametersJson = kParameters;
  definition.execute = executeRead;
  definition.timeoutMs = 30000;
  // 纯读: 可与兄弟调用重叠。
  definition.executionMode = [](const std::string&) { return ExecutionMode::Parallel; };
  definition.presentCall = presentRead;
  return definition;
}

}