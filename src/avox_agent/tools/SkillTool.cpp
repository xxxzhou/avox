#include "SkillTool.hpp"

#include <string>

#include "ToolArgs.hpp"
#include "avox/module/LogHelper.hpp"
#include "avox_agent/skills/SkillRegistry.hpp"

namespace avox {

namespace {

// 对齐 dsh tool-skill 的入参: 唯一参数 name, 必填。
constexpr const char* kParameters = R"json({
  "type": "object",
  "properties": {
    "name": {"type": "string", "description": "The exact skill name from the available skills list."}
  },
  "required": ["name"]
})json";

ToolResult executeSkill(const ToolExecution& exec) {
  LOGFLF(LogLevel::info, "agent tool skill");
  const Json args = parseToolArgs(exec.argumentsJson);
  const std::string name = toolArgString(args, "name");

  // 对齐 dsh 的 skill 工具语义: 名不合法 / 未知 / 模型不可调 → 直接报错 (工具只加载正文,
  // 模型需要看到明确失败才能修正参数或换一条 skill)。
  if (!isSkillName(name)) {
    return toolError(ToolOutcome::Fatal,
                     "invalid skill name \"" + name + "\"",
                     TOOL_CODE_INVALID_ARGS);
  }
  Skill* skill = builtinSkillRegistry().find(name);
  if (skill == nullptr) {
    return toolError(ToolOutcome::Fatal,
                     "skill \"" + name + "\" is unknown or no longer available",
                     "UNKNOWN_SKILL");
  }
  if (!skill->invocation.modelInvocable) {
    return toolError(ToolOutcome::Fatal,
                     "skill \"" + name + "\" is not available for model invocation",
                     "NOT_MODEL_INVOCABLE");
  }
  return toolOk(skill->renderSkillContent());
}

std::optional<ToolCallView> presentSkill(const std::string& argumentsJson) {
  const Json args = parseToolArgs(argumentsJson);
  const std::string name = toolArgString(args, "name");
  if (name.empty()) return std::nullopt;

  GenericCallCard card;
  card.kind = ToolCallKind::Read;
  card.title = "Load skill " + name;
  return ToolCallView{card};
}

}  // namespace

ToolDefinition makeSkillTool() {
  ToolDefinition definition;
  definition.name = "skill";
  definition.description =
      "Load the full instructions for an available skill. Call this with the exact skill "
      "name from the session skill catalog before acting on a task that names or clearly "
      "matches that skill.";
  definition.parametersJson = kParameters;
  definition.execute = executeSkill;
  definition.timeoutMs = 5000;   // 纯内存加载, 不碰外部 IO
  definition.presentCall = presentSkill;
  return definition;
}

}
