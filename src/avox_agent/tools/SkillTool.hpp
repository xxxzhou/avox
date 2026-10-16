#pragma once

#include "avox_agent/core/ToolTypes.hpp"

namespace avox {

// skill 工具 (对齐 dsh 的 tool-skill): 唯一的 skill 加载器。
// 模型先看 system prompt 里的 skill 目录, 选中后用这个工具按 name 加载一条 skill 的
ToolDefinition makeSkillTool();

}
