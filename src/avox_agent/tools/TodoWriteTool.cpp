#include "TodoWriteTool.hpp"

#include <string>
#include <utility>
#include <vector>

#include "ToolArgs.hpp"
#include "avox/module/Json.hpp"
#include "avox/module/LogHelper.hpp"
#include "avox_agent/core/Agent.hpp"
#include "avox_agent/core/Session.hpp"
#include "avox_agent/core/Agent.hpp"

namespace avox {

namespace {

constexpr const char* kParameters = R"json({
    "type": "object",
    "properties": {
      "todos": {
        "type": "array",
        "description": "完整的 todo 列表快照 (整表替换, 不是增量): [{content, status}], status ∈ pending|in_progress|completed",
        "items": {
          "type": "object",
          "properties": {
            "content": {"type": "string", "description": "任务描述 (非空)"},
            "status": {"type": "string", "enum": ["pending", "in_progress", "completed"], "description": "任务状态"}
          },
          "required": ["content", "status"]
        }
      }
    },
    "required": ["todos"]
  })json";

ToolResult executeTodoWrite(const ToolExecution& exec) {
  LOGFLF(LogLevel::info, "agent tool todo_write");
  // dsh: todo 列表挂在会话上, 没有属主会话就不是拒绝而是不可用。
  if (exec.agent == nullptr) {
    return toolError(ToolOutcome::Fatal,
                     "todo_write requires an owning agent session",
                     TOOL_CODE_INVALID_ARGS);
  }
  const Json args = parseToolArgs(exec.argumentsJson);
  if (!args.bObject() || !args.find("todos") || !args["todos"].bArray()) {
    return toolError(ToolOutcome::Fatal,
                     "invalid todos: expected an array of {content, status}",
                     TOOL_CODE_INVALID_ARGS);
  }

  std::vector<TodoItem> items;
  std::vector<std::string> seen;
  const Json& todos = args["todos"];
  for (size_t i = 0; i < todos.size(); ++i) {
    const Json& entry = todos.at(i);
    if (!entry.bObject() || !entry.find("content") || !entry["content"].bString()
        || entry["content"].get<std::string>().empty()) {
      return toolError(ToolOutcome::Fatal,
                       "invalid todo: `content` must be a non-empty string",
                       TOOL_CODE_INVALID_ARGS);
    }
    TodoItem item;
    item.content = entry["content"].get<std::string>();
    const std::string status = toolArgString(entry, "status");
    if (status == "pending") {
      item.status = TodoStatus::Pending;
    } else if (status == "in_progress") {
      item.status = TodoStatus::InProgress;
    } else if (status == "completed") {
      item.status = TodoStatus::Completed;
    } else {
      return toolError(ToolOutcome::Fatal,
                       "invalid todo: `status` must be one of \"pending\", "
                       "\"in_progress\", \"completed\"",
                       TOOL_CODE_INVALID_ARGS);
    }
    // dsh: 内容重复即坏快照 (列表是身份, 重影会让进度不可读)。
    for (const std::string& prior : seen) {
      if (prior == item.content) {
        return toolError(ToolOutcome::Fatal,
                         "invalid todos: duplicate content \"" + item.content + "\"",
                         TOOL_CODE_INVALID_ARGS);
      }
    }
    seen.push_back(item.content);
    items.push_back(std::move(item));
  }
  // 注: dsh 部署侧开 allowParallelInProgress (多条 in_progress 合法), avox 沿用。

  // 写日志事件 (仅记日志, 不上 surface); 会话访问走 withSession 串行化。
  const std::vector<TodoItem> snapshot = items;
  exec.agent->withSession([&snapshot](Session& session) {
    session.append(TodoWriteData{snapshot});
  });

  int pending = 0;
  int inProgress = 0;
  int completed = 0;
  for (const TodoItem& item : items) {
    if (item.status == TodoStatus::Pending) {
      pending++;
    } else if (item.status == TodoStatus::InProgress) {
      inProgress++;
    } else {
      completed++;
    }
  }
  // dsh render 的固定文案。
  ToolResult result = toolOk("Updated todo list: " + std::to_string(pending)
                             + " pending, " + std::to_string(inProgress)
                             + " in progress, " + std::to_string(completed)
                             + " completed.");
  Json meta(Json::JsonObject{});
  Json jsonTodos(Json::JsonArray{});
  for (const TodoItem& item : items) {
    Json entry(Json::JsonObject{});
    entry["content"] = item.content;
    entry["status"] = item.status == TodoStatus::Pending ? "pending"
                      : item.status == TodoStatus::InProgress ? "in_progress"
                                                              : "completed";
    jsonTodos.push_back(std::move(entry));
  }
  meta["todos"] = std::move(jsonTodos);
  Json counts(Json::JsonObject{});
  counts["pending"] = static_cast<int64_t>(pending);
  counts["inProgress"] = static_cast<int64_t>(inProgress);
  counts["completed"] = static_cast<int64_t>(completed);
  meta["counts"] = std::move(counts);
  result.meta = meta.dump();
  return result;
}

}  // namespace

ToolDefinition makeTodoWriteTool() {
  ToolDefinition definition;
  definition.name = "todo_write";
  definition.description =
      "写入完整的 todo 列表快照 (整表替换: 传当前全部任务, 不是增量)。每条 {content, "
      "status∈pending|in_progress|completed}; 开始做一项就标 in_progress, 做完标 "
      "completed。列表是会话展示层状态, 不进模型历史。";
  definition.parametersJson = kParameters;
  definition.execute = executeTodoWrite;
  definition.timeoutMs = 10000;
  // executionMode 不设 = 独占 (dsh todo_write 未声明 isConcurrencySafe)。
  return definition;
}

}
