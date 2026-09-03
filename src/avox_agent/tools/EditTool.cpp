#include "EditTool.hpp"

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
      "old_string": {"type": "string", "description": "被替换的字面文本 (非空; 必须在文件中唯一, 除非 replace_all)"},
      "new_string": {"type": "string", "description": "替换后的文本"},
      "replace_all": {"type": "boolean", "description": "true 时替换全部命中 (默认 false: 恰好一次)"}
    },
    "required": ["file_path", "old_string", "new_string"]
  })json";

ToolResult executeEdit(const ToolExecution& exec) {
  LOGFLF(LogLevel::info, "agent tool edit");
  const Json args = parseToolArgs(exec.argumentsJson);
  const std::string rawPath = toolArgString(args, "file_path");
  const std::string oldString = toolArgString(args, "old_string");
  const std::string newString = toolArgString(args, "new_string");
  const bool replaceAll = toolArgBool(args, "replace_all", false);

  if (rawPath.empty()) {
    return toolError(ToolOutcome::Fatal, "file_path must be a non-empty string",
                     TOOL_CODE_INVALID_ARGS);
  }
  // dsh edit.ts 的参数序: 先查 old_string 非空, 再查 old/new 不同 (原文比较, 不做行尾规范化)。
  const std::string oldNorm = normalizeLf(oldString);
  if (oldNorm.empty()) {
    return toolError(ToolOutcome::Fatal, "old_string must be a non-empty string",
                     TOOL_CODE_INVALID_ARGS);
  }
  if (oldString == newString) {
    return toolError(ToolOutcome::Fatal, "old_string and new_string must differ",
                     TOOL_CODE_INVALID_ARGS);
  }

  const std::filesystem::path path = resolveAgentPath(rawPath);
  // dsh 的错误文案与成功文案都引 displayPath (后端解析的绝对路径), 不是原始入参。
  const std::string displayPath = path.string();
  const std::optional<std::string> raw = readWholeFile(path);
  if (!raw.has_value()) {
    // dsh 的「读不到」走观察策略文案; avox 没有那套 read-observation 层, 用直白事实。
    return toolError(ToolOutcome::Fatal, "无法读取文件: " + displayPath,
                     "FILE_NOT_READABLE");
  }
  if (raw->find('\0') != std::string::npos) {
    return toolError(ToolOutcome::Fatal, "cannot edit \"" + displayPath
                                             + "\": binary file",
                     "FS_NOT_TEXT");
  }

  // dsh applyLiteralEdit: 匹配双方都先 LF 规范化; 命中计数决定拒绝还是替换。
  const std::string content = normalizeLf(*raw);
  const std::string newNorm = normalizeLf(newString);
  size_t replacements = 0;
  for (size_t pos = 0;
       (pos = content.find(oldNorm, pos)) != std::string::npos;
       pos += oldNorm.size()) {
    replacements++;
  }
  if (replacements == 0) {
    return toolError(ToolOutcome::Fatal,
                     "old_string was not found in \"" + displayPath + "\"",
                     "FS_EDIT_NOT_FOUND");
  }
  if (!replaceAll && replacements > 1) {
    return toolError(
        ToolOutcome::Fatal,
        "old_string matched " + std::to_string(replacements) + " times in \""
            + displayPath + "\"; provide a more specific old_string or set"
            " replace_all to true",
        "FS_AMBIGUOUS_EDIT");
  }

  // split/join 式替换 (等价 dsh 的 content.split(old).join(new))。
  std::string edited;
  edited.reserve(content.size() + newNorm.size());
  size_t pos = 0;
  while (true) {
    const size_t hit = content.find(oldNorm, pos);
    if (hit == std::string::npos) {
      edited += content.substr(pos);
      break;
    }
    edited += content.substr(pos, hit - pos);
    edited += newNorm;
    pos = hit + oldNorm.size();
  }

  // 写回恢复读取时探测到的主流行尾 (CRLF 时先再规范化, 防 \r\r\n)。
  const std::string output =
      checkCrlfDominant(*raw) ? restoreCrlf(edited) : edited;
  if (!writeWholeFileAtomic(path, output)) {
    return toolError(ToolOutcome::Fatal, "无法写入文件: " + displayPath,
                     "FILE_NOT_WRITABLE");
  }

  // dsh formatEditOutput 的两句固定文案。
  ToolResult result = toolOk(
      replaceAll
          ? "The file " + displayPath
                + " has been updated. All occurrences were successfully replaced."
          : "The file " + displayPath + " has been updated successfully.");
  Json meta(Json::JsonObject{});
  meta["path"] = displayPath;
  meta["replacements"] = static_cast<int64_t>(replacements);
  result.meta = meta.dump();
  return result;
}

std::optional<ToolCallView> presentEdit(const std::string& argumentsJson) {
  const Json args = parseToolArgs(argumentsJson);
  const std::string path = toolArgString(args, "file_path");
  const std::string oldString = toolArgString(args, "old_string");
  const std::string newString = toolArgString(args, "new_string");
  if (path.empty()) return std::nullopt;
  // 调用期就能给出替换段落的迷你差异 (dsh 的 before/after 全文差异留给回放层,
  // 这里 old_string 即被改区域)。
  DiffCallCard card;
  FileDiff diff;
  diff.path = path;
  if (!oldString.empty()) diff.oldText = oldString;
  diff.newText = newString;
  card.diffs.push_back(std::move(diff));
  return ToolCallView{card};
}

}  // namespace

ToolDefinition makeEditTool() {
  ToolDefinition definition;
  definition.name = "edit";
  definition.description =
      "对文件做字面字符串替换 (非正则)。old_string 默认必须在文件中恰好命中一次; "
      "多处命中会报错并告知命中次数 (改得更具体或设 replace_all)。"
      "匹配与替换在 LF 规范化内容上做, 写回保留文件原行尾风格。";
  definition.parametersJson = kParameters;
  definition.execute = executeEdit;
  definition.timeoutMs = 30000;
  definition.presentCall = presentEdit;
  // executionMode 不设 = 独占 (dsh edit 未声明 isConcurrencySafe)。
  return definition;
}

}
