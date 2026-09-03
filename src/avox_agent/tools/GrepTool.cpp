#include "GrepTool.hpp"

#include <fstream>
#include <regex>
#include <string>
#include <utility>
#include <vector>

#include "ToolArgs.hpp"
#include "avox/Avox.hpp"  // getAvoxPath / expandEnvPath
#include "avox/module/LogHelper.hpp"

namespace avox {

namespace {

constexpr const char* kParameters = R"json({
    "type": "object",
    "properties": {
      "path": {"type": "string", "description": "文件路径; 仅文件名(无路径分隔符)默认取 <运行目录>/logs/"},
      "pattern": {"type": "string", "description": "正则表达式(始终按正则处理, ripgrep 语法)"},
      "max": {"type": "integer", "description": "最多返回匹配行数(默认 50, 上限 500)"},
      "window": {"type": "integer", "description": "长行按首个匹配前后截取的字符窗宽(默认 200)"}
    },
    "required": ["path", "pattern"]
  })json";

// 字节偏移回退/前进到 UTF-8 字符边界。
//
// 不做这一步, 窗口截断会在多字节字符中间切开 —— 而中文日志里这几乎必然发生。
size_t toCharBoundary(const std::string& text, size_t pos, bool forward) {
  if (forward) {
    while (pos < text.size()
           && (static_cast<unsigned char>(text[pos]) & 0xC0) == 0x80) {
      ++pos;
    }
  } else {
    while (pos > 0 && (static_cast<unsigned char>(text[pos]) & 0xC0) == 0x80) {
      --pos;
    }
  }
  return pos;
}

ToolResult executeGrep(const ToolExecution& exec) {
  const Json args = parseToolArgs(exec.argumentsJson);
  std::string path = toolArgString(args, "path");
  const std::string pattern = toolArgString(args, "pattern");

  // pattern 恒按正则, 与 dsh 的 grep 契约一致 (不再有 regex 开关)。
  int max = toolArgInt(args, "max", 50);
  int window = toolArgInt(args, "window", 200);
  if (max > 500) max = 500;
  if (max < 1) max = 1;
  if (window < 40) window = 40;

  if (path.empty() || pattern.empty()) {
    return toolError(ToolOutcome::Fatal,
                     "grep 缺少 path 或 pattern 参数 (均必填)。"
                     "例: grep({path:\"<日志路径>\", pattern:\"webcamName\"})。",
                     TOOL_CODE_INVALID_ARGS);
  }
  if (path.find('/') == std::string::npos && path.find('\\') == std::string::npos) {
    path = getAvoxPath() + "/logs/" + path;
  }
  const std::string displayPath = path;
  path = expandEnvPath(path);

  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return toolError(ToolOutcome::Fatal, "无法读取文件: " + path, "FILE_NOT_READABLE");
  }

  std::regex expression;
  try {
    expression = std::regex(pattern);
  } catch (...) {
    return toolError(ToolOutcome::Fatal, "正则表达式非法: " + pattern,
                     TOOL_CODE_INVALID_ARGS);
  }

  // 每条保留匹配的 (1-based 行号, 窗口片段)。
  std::vector<std::pair<int, std::string>> rows;
  std::string line;
  int lineNo = 0;   // 1-based, 与 read 的 offset 一致
  int matched = 0;
  while (std::getline(file, line)) {
    lineNo++;
    std::smatch match;
    if (std::regex_search(line, match, expression)) {
      matched++;
      if (matched <= max) {
        const size_t pos = static_cast<size_t>(match.position());
        const size_t length = line.size();
        const size_t half = static_cast<size_t>(window) / 2;
        size_t start = pos > half ? pos - half : 0;
        size_t end = pos + half;
        if (end > length) end = length;
        start = toCharBoundary(line, start, false);
        end = toCharBoundary(line, end, true);
        std::string snippet = (start > 0 ? "..." : "") + line.substr(start, end - start)
                              + (end < length ? "...(截断)" : "");
        rows.emplace_back(lineNo, std::move(snippet));
      }
    }
  }

  std::string result;
  if (matched == 0) {
    result = "No matches found (pattern=\"" + pattern + "\", 共 "
             + std::to_string(lineNo) + " 行)。可换更宽松的正则或换文件。";
  } else {
    // 对齐 dsh: 头部 Found N matches, 再按文件分组, 每行 "Line N: <snippet>"。
    result = "Found " + std::to_string(matched) + " matches\n\n" + displayPath;
    for (const auto& [number, snippet] : rows) {
      result += "\nLine " + std::to_string(number) + ": " + snippet;
    }
    if (matched > max) {
      result += "\n\n(超过上限 " + std::to_string(max) + ", 仅保留前 "
                + std::to_string(max)
                + " 行; 请细化 pattern 或调大 max)";
    }
  }
  return toolOk(std::move(result));
}

std::optional<ToolCallView> presentGrep(const std::string& argumentsJson) {
  const Json args = parseToolArgs(argumentsJson);
  const std::string path = toolArgString(args, "path");
  const std::string pattern = toolArgString(args, "pattern");
  if (path.empty() && pattern.empty()) return std::nullopt;

  GenericCallCard card;
  card.kind = ToolCallKind::Search;
  card.title = "Grep " + pattern + (path.empty() ? "" : " in " + path);
  card.content = pattern;
  if (!path.empty()) card.locations.push_back(FileLocation{path, std::nullopt});
  return ToolCallView{card};
}

}  // namespace

ToolDefinition makeGrepTool() {
  ToolDefinition definition;
  definition.name = "grep";
  definition.description =
      "用正则搜索文件行(pattern 始终按正则), 返回匹配行号与片段, 大日志里定位特定行比逐段 read 更快。";
  definition.parametersJson = kParameters;
  definition.execute = executeGrep;
  definition.timeoutMs = 60000;
  definition.executionMode = [](const std::string&) { return ExecutionMode::Parallel; };
  definition.presentCall = presentGrep;
  return definition;
}

}