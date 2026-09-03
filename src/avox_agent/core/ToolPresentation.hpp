#pragma once

// ============================================================================
// 工具调用的 UI 渲染意图。
//
// 对齐 dsh 的 packages/core/tools/src/presentation.ts。
//
// 为什么渲染意图属于工具自己声明: UI 会在**实时流**和**日志回放**两条路上渲染同一次调用,
// 两条路必须画出同一张卡。因此 presentCall / presentResult 必须是纯函数, 只依赖 args
// (与持久化的 meta), 不能读任何运行期状态。
//
// 无法从模型面文本无损重建的结构 (grep 的命中行号、诊断的时间轴) 通过 ToolResult::meta
// 投影成 JSON 存进 tool/result 事件, 回放时读回来 —— 这就是 meta 存在的唯一理由。
//
// 降级是设计的一部分: 不支持某张卡的 UI 一律回退到 tool/result 的原始 content。
// ============================================================================

#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "avox/AvoxDef.h"

namespace avox {

// 编辑器跟随用: 工具声明自己读了或改了哪个文件的哪一行。
struct FileLocation {
  std::string path;
  // 1 起的行号; 不填表示整文件。
  std::optional<int> line;
};

// 通用卡片的动作分类 (决定图标与措辞)。
enum class ToolCallKind {
  Read,
  Edit,
  Delete,
  Move,
  Search,
  Execute,
  Fetch,
  Other,
};

// 一处文件差异。
struct FileDiff {
  std::string path;
  // 新建或整体覆盖时不填 —— 调用期的 presenter 看不到旧内容。
  std::optional<std::string> oldText;
  std::string newText;
};

// ---------------------------------------------------------------------------
// 调用期卡片
// ---------------------------------------------------------------------------

struct GenericCallCard {
  std::string title;
  ToolCallKind kind = ToolCallKind::Other;
  // 给「查看原始入参」用; 不填则由 UI 自行决定是否展示。
  std::optional<std::string> rawInput;
  // 摘要正文 (例如被搜索的模式)。
  std::optional<std::string> content;
  std::vector<FileLocation> locations;
};

struct TerminalCallCard {
  // 命令原文作标题。
  std::string command;
  // 绝对路径直接用; 相对路径由桥接层相对会话工作目录解析。
  std::optional<std::string> cwd;
};

struct DiffCallCard {
  std::vector<FileDiff> diffs;
};

using ToolCallView =
    std::variant<GenericCallCard, TerminalCallCard, DiffCallCard>;

// ---------------------------------------------------------------------------
// 结果期卡片
// ---------------------------------------------------------------------------

struct GenericResultCard {
  // 不填则保留调用期的标题。
  std::optional<std::string> title;
  std::optional<std::string> content;
};

struct TerminalResultCard {
  std::optional<std::string> title;
  std::string output;
  std::optional<int> exitCode;
};

struct DiffResultCard {
  std::optional<std::string> title;
  std::vector<FileDiff> diffs;
};

// 搜索结果。故意不带结果文本 —— 命中列表由 UI 按结构渲染, 塞一份文本进来只会两处不一致。
struct SearchResultCard {
  std::optional<std::string> title;
  enum class Shape { Matches, Paths } shape = Shape::Matches;
  std::vector<FileLocation> hits;
  // 命中总数 (被截断时大于 hits.size())。
  std::optional<int> total;
  bool truncated = false;
};

struct ReadResultCard {
  std::optional<std::string> title;
  std::string lines;
  // 0 起的起始行偏移。
  int offset = 0;
  std::optional<int> totalLines;
  // 语法高亮用的语言标识。
  std::optional<std::string> lang;
};

struct WebResultCard {
  std::optional<std::string> title;
  std::string url;
  std::optional<std::string> content;
};

using ToolResultView =
    std::variant<GenericResultCard, TerminalResultCard, DiffResultCard,
                 SearchResultCard, ReadResultCard, WebResultCard>;

}
