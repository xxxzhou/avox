#include "GlobTool.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "ToolArgs.hpp"
#include "ToolIo.hpp"
#include "avox/module/Json.hpp"
#include "avox/module/LogHelper.hpp"

namespace avox {

namespace {

// dsh GLOB_MAX_RESULTS。
constexpr size_t kMaxResults = 100;
// dsh GLOB_VCS_EXCLUDES: 这些目录名在遍历时整个跳过 (不进结果也不递归)。
const std::vector<std::string> kVcsExcludes = {
    ".git", ".svn", ".hg", ".bzr", ".jj", ".sl",
};

constexpr const char* kParameters = R"json({
    "type": "object",
    "properties": {
      "pattern": {"type": "string", "description": "glob 模式 (如 \"**/*.ts\", \"src/**/*.test.js\")。不含 / 时按文件名在任意深度匹配 (\"*.ts\" 搜全树); 含 / 时锚定相对深度 (** 跨目录, * 不跨)"},
      "path": {"type": "string", "description": "搜索根目录 (默认当前工作目录; 相对路径按会话工作目录解析)"}
    },
    "required": ["pattern"]
  })json";

// 展开花括号 {a,b} (递归支持嵌套); 无花括号时原样返回单元素。
std::vector<std::string> expandBraces(const std::string& pattern) {
  const size_t open = pattern.find('{');
  if (open == std::string::npos) return {pattern};
  // 找与 open 配对的闭括号 (计数嵌套)。
  int depth = 0;
  size_t close = std::string::npos;
  for (size_t i = open; i < pattern.size(); ++i) {
    if (pattern[i] == '{') {
      depth++;
    } else if (pattern[i] == '}') {
      depth--;
      if (depth == 0) {
        close = i;
        break;
      }
    }
  }
  if (close == std::string::npos) return {pattern};
  // 顶层 (深度 1) 按逗号拆分; 空段允许 ({,a} 里空段 = 无前缀)。
  const std::string prefix = pattern.substr(0, open);
  const std::string suffix = pattern.substr(close + 1);
  std::vector<std::string> parts;
  {
    size_t start = open + 1;
    depth = 0;
    for (size_t i = open + 1; i < close; ++i) {
      if (pattern[i] == '{') depth++;
      if (pattern[i] == '}') depth--;
      if (pattern[i] == ',' && depth == 0) {
        parts.push_back(pattern.substr(start, i - start));
        start = i + 1;
      }
    }
    parts.push_back(pattern.substr(start, close - start));
  }
  std::vector<std::string> out;
  for (const std::string& part : parts) {
    for (std::string& expanded : expandBraces(prefix + part + suffix)) {
      out.push_back(std::move(expanded));
    }
  }
  return out;
}

// 单个 glob 模式对文本匹配 (回溯递归; 分隔符恒按 '/')。
//   **     跨目录任意序列; "**/" 额外匹配零个目录 (ripgrep 语义)
//   *      任意非 '/' 序列
//   ?      单个非 '/' 字符
bool globMatch(const std::string& pattern, size_t p, const std::string& text,
               size_t t) {
  if (p == pattern.size()) return t == text.size();
  if (p + 1 < pattern.size() && pattern[p] == '*' && pattern[p + 1] == '*') {
    const size_t rest = p + 2;
    if (rest < pattern.size() && pattern[rest] == '/') {
      // "**/": 零目录 (直接跳过) 或 ≥1 目录 (吃到下一个 '/')。
      if (globMatch(pattern, rest + 1, text, t)) return true;
      const size_t slash = text.find('/', t);
      if (slash == std::string::npos) return false;
      return globMatch(pattern, p, text, slash + 1);
    }
    // 独立的 "**": 吃任意前缀 (含 '/')。
    for (size_t i = t; i <= text.size(); ++i) {
      if (globMatch(pattern, rest, text, i)) return true;
    }
    return false;
  }
  if (pattern[p] == '*') {
    // 单 '*': 不跨 '/', 消耗 0..n 个非 '/' 字符。
    for (size_t i = t; i <= text.size(); ++i) {
      if (i > t && text[i - 1] == '/') break;
      if (globMatch(pattern, p + 1, text, i)) return true;
    }
    return false;
  }
  if (pattern[p] == '?') {
    if (t >= text.size() || text[t] == '/') return false;
    return globMatch(pattern, p + 1, text, t + 1);
  }
  if (t >= text.size() || text[t] != pattern[p]) return false;
  return globMatch(pattern, p + 1, text, t + 1);
}

struct GlobHit {
  std::string rel;                        // 相对根的路径 ('/' 分隔)
  std::filesystem::file_time_type mtime;  // 排序键
};

ToolResult executeGlob(const ToolExecution& exec) {
  LOGFLF(LogLevel::info, "agent tool glob");
  const Json args = parseToolArgs(exec.argumentsJson);
  const std::string pattern = toolArgString(args, "pattern");
  const std::string rawPath = toolArgString(args, "path");
  if (pattern.empty()) {
    return toolError(ToolOutcome::Fatal, "pattern must be a non-empty string",
                     TOOL_CODE_INVALID_ARGS);
  }
  if (!rawPath.empty() && rawPath.find_first_not_of(" \t\r\n") == std::string::npos) {
    return toolError(ToolOutcome::Fatal,
                     "path must be a non-empty string when given",
                     TOOL_CODE_INVALID_ARGS);
  }

  // 根: 未给 path = 当前工作目录 (输出 root 记 "."); 给了则解析为绝对路径。
  const std::filesystem::path base = rawPath.empty()
                                         ? std::filesystem::current_path()
                                         : resolveAgentPath(rawPath);
  std::error_code ec;
  if (!std::filesystem::is_directory(base, ec)) {
    return toolError(ToolOutcome::Fatal, "搜索根目录不存在: " + base.string(),
                     "PATH_NOT_FOUND");
  }

  // 花括号展开成多个模式; 无 '/' 的模式按文件名匹配 (任意深度), 否则按相对路径匹配。
  const std::vector<std::string> patterns = expandBraces(pattern);
  const bool basenameMode = pattern.find('/') == std::string::npos;
  std::vector<GlobHit> hits;
  std::vector<std::filesystem::path> stack{base};
  while (!stack.empty()) {
    if (exec.signal != nullptr && exec.signal->aborted()) {
      return toolError(ToolOutcome::Aborted, "glob 遍历被取消", TOOL_CODE_ABORTED);
    }
    const std::filesystem::path dir = stack.back();
    stack.pop_back();
    std::error_code listEc;
    std::filesystem::directory_iterator it(dir, listEc), end;
    if (listEc) continue;
    for (; it != end; it.increment(listEc)) {
      if (listEc) break;
      const std::filesystem::directory_entry entry = *it;
      std::error_code typeEc;
      // rg 不跟符号链接: 符号链接 (文件/目录) 一律跳过, 也防环。
      if (entry.is_symlink(typeEc)) continue;
      if (entry.is_directory(typeEc)) {
        const std::string name = entry.path().filename().string();
        if (std::find(kVcsExcludes.begin(), kVcsExcludes.end(), name)
                != kVcsExcludes.end()) {
          continue;
        }
        stack.push_back(entry.path());
        continue;
      }
      if (!entry.is_regular_file(typeEc)) continue;
      const std::string rel =
          std::filesystem::relative(entry.path(), base, ec).generic_string();
      if (ec) continue;
      bool matched = false;
      for (const std::string& one : patterns) {
        if (basenameMode) {
          const std::string name = entry.path().filename().generic_string();
          if (globMatch(one, 0, name, 0)) matched = true;
        } else if (globMatch(one, 0, rel, 0)) {
          matched = true;
        }
        if (matched) break;
      }
      if (!matched) continue;
      GlobHit hit;
      hit.rel = rel;
      hit.mtime = entry.last_write_time(ec);
      if (ec) continue;
      hits.push_back(std::move(hit));
    }
  }

  if (hits.empty()) return toolOk("No files found");
  // dsh 契约: 修改时间升序 (同名次按路径稳定排序); 超上限取时间序头部。
  std::sort(hits.begin(), hits.end(), [](const GlobHit& a, const GlobHit& b) {
    if (a.mtime != b.mtime) return a.mtime < b.mtime;
    return a.rel < b.rel;
  });
  std::vector<std::string> shown;
  const size_t seen = hits.size();
  for (size_t i = 0; i < seen && i < kMaxResults; ++i) shown.push_back(hits[i].rel);

  std::string body;
  for (size_t i = 0; i < shown.size(); ++i) {
    if (i > 0) body += '\n';
    body += shown[i];
  }
  ToolResult result;
  if (seen <= kMaxResults) {
    result = toolOk(std::move(body));
  } else {
    // dsh formatGlobPage 的平铺截断支 (无 spill 文件, recovery 固定为窄化提示)。
    result = toolOk(body + "\n\n(Showing " + std::to_string(shown.size()) + " of "
                      + std::to_string(seen) + " paths. The complete result could"
                        " not be saved; narrow pattern or path to see more.)");
  }
  // dsh output 形状 {root, paths} 进 meta, 供结果卡片与回放重建 (root 记调用方给的形态)。
  Json meta(Json::JsonObject{});
  meta["root"] = rawPath.empty() ? std::string(".") : rawPath;
  Json paths(Json::JsonArray{});
  for (const std::string& rel : shown) paths.push_back(Json(rel));
  meta["paths"] = std::move(paths);
  result.meta = meta.dump();
  return result;
}

std::optional<ToolResultView> presentGlobResult(const std::string& argumentsJson,
                                                const ToolResult& result) {
  SearchResultCard card;
  card.shape = SearchResultCard::Shape::Paths;
  std::string text;
  if (!result.content.empty()) {
    if (const auto* block = std::get_if<TextBlock>(&result.content[0])) {
      text = block->text;
    }
  }
  // 回放路径: 从 meta 拿 {root, paths}; 实时路径 (meta 缺失) 从正文按行还原。
  Json meta;
  if (result.meta.has_value()) meta = parseToolArgs(*result.meta);
  if (meta.bObject() && meta.find("paths") && meta["paths"].bArray()) {
    const Json& paths = meta["paths"];
    for (size_t i = 0; i < paths.size(); ++i) {
      if (paths.at(i).bString()) {
        card.hits.push_back(FileLocation{paths.at(i).get<std::string>(),
                                         std::nullopt});
      }
    }
  } else {
    size_t start = 0;
    while (start < text.size()) {
      size_t end = text.find('\n', start);
      if (end == std::string::npos) end = text.size();
      if (end > start) {
        card.hits.push_back(FileLocation{text.substr(start, end - start),
                                         std::nullopt});
      }
      start = end + 1;
    }
  }
  card.truncated = text.find("Showing ") != std::string::npos;
  return ToolResultView{card};
}

std::optional<ToolCallView> presentGlob(const std::string& argumentsJson) {
  const Json args = parseToolArgs(argumentsJson);
  const std::string pattern = toolArgString(args, "pattern");
  const std::string path = toolArgString(args, "path");
  if (pattern.empty()) return std::nullopt;
  GenericCallCard card;
  card.kind = ToolCallKind::Search;
  card.title = "Glob " + pattern + (path.empty() ? "" : " in " + path);
  card.content = pattern;
  return ToolCallView{card};
}

}  // namespace

ToolDefinition makeGlobTool() {
  ToolDefinition definition;
  definition.name = "glob";
  definition.description =
      "按 glob 模式找文件, 返回匹配的文件路径 —— 不含目录, 含隐藏文件, 排除 .git 等"
      "版本库目录。最多 100 条, 按修改时间升序; 超出时截断并说明。"
      "模式不含 / 时按文件名在任意深度匹配; 含 / 时锚定相对深度。";
  definition.parametersJson = kParameters;
  definition.execute = executeGlob;
  definition.timeoutMs = 60000;
  definition.presentCall = presentGlob;
  definition.presentResult = presentGlobResult;
  // executionMode 不设 = 独占 (dsh glob 未声明 isConcurrencySafe)。
  return definition;
}

}
