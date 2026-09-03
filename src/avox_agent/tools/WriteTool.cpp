#include "WriteTool.hpp"

#include <filesystem>
#include <string>
#include <utility>

#include "ToolArgs.hpp"
#include "ToolIo.hpp"
#include "avox/module/Json.hpp"
#include "avox/module/LogHelper.hpp"

namespace avox {

namespace {

constexpr const char* kParameters = R"json({
    "type": "object",
    "properties": {
      "file_path": {"type": "string", "description": "目标文件路径 (相对路径按会话工作目录解析)"},
      "content": {"type": "string", "description": "完整文件内容 (整体覆盖, 不是追加)"}
    },
    "required": ["file_path", "content"]
  })json";

ToolResult executeWrite(const ToolExecution& exec) {
  LOGFLF(LogLevel::info, "agent tool write");
  const Json args = parseToolArgs(exec.argumentsJson);
  const std::string rawPath = toolArgString(args, "file_path");
  if (rawPath.empty()) {
    return toolError(ToolOutcome::Fatal, "file_path must be a non-empty string",
                     TOOL_CODE_INVALID_ARGS);
  }
  // content 必须显式给字符串 (允许空串 = 清空文件), 与 dsh 的必填校验一致。
  if (!args.bObject() || !args.find("content") || !args["content"].bString()) {
    return toolError(ToolOutcome::Fatal, "content must be a string",
                     TOOL_CODE_INVALID_ARGS);
  }
  const std::string content = args["content"].get<std::string>();

  const std::filesystem::path path = resolveAgentPath(rawPath);
  const bool existed = std::filesystem::exists(path);
  if (!writeWholeFileAtomic(path, content)) {
    return toolError(ToolOutcome::Fatal, "无法写入文件: " + path.string(),
                     "FILE_NOT_WRITABLE");
  }

  // dsh formatWriteOutput 的信封; <path> 与 output.path 都是后端解析出的绝对路径
  // (dsh fs-local: displayPath = resolve(cwd, path)), 不是调用方给的原始形态。
  const std::string displayPath = path.string();
  const char* verb = existed ? "Updated file" : "Created file";
  ToolResult result = toolOk("<path>" + displayPath + "</path>\n<type>file</type>\n<content>\n"
                             + verb + "\n</content>");
  // meta 携带 dsh output 的 {path, operation}; before/after 全文不进日志 ——
  // 那是展示层差异的输入, 而 avox 的调用期卡片从入参就能重建差异。
  Json meta(Json::JsonObject{});
  meta["path"] = displayPath;
  meta["operation"] = existed ? "update" : "create";
  result.meta = meta.dump();
  return result;
}

std::optional<ToolCallView> presentWrite(const std::string& argumentsJson) {
  const Json args = parseToolArgs(argumentsJson);
  const std::string path = toolArgString(args, "file_path");
  if (path.empty()) return std::nullopt;
  GenericCallCard card;
  card.kind = ToolCallKind::Edit;
  card.title = "Write " + path;
  card.locations.push_back(FileLocation{path, std::nullopt});
  return ToolCallView{card};
}

}  // namespace

ToolDefinition makeWriteTool() {
  ToolDefinition definition;
  definition.name = "write";
  definition.description =
      "整体写入文件 (content 是完整内容, 新建或覆盖; 父目录缺失会创建)。"
      "相对路径按会话工作目录解析。返回创建/更新确认信封。";
  definition.parametersJson = kParameters;
  definition.execute = executeWrite;
  definition.timeoutMs = 30000;
  definition.presentCall = presentWrite;
  // executionMode 不设 = 独占 (dsh write 未声明 isConcurrencySafe)。
  return definition;
}

}
