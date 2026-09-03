#pragma once

#include "avox_agent/core/ToolTypes.hpp"

namespace avox {

// skill 工具 (对齐 dsh 的 tool-skill): 唯一的 skill 加载器。
//
// 模型先看 system prompt 里的 skill 目录, 选中后用这个工具按 name 加载一条 skill 的
// 完整指令正文 (<skill_content> 块)。skill 只是加载正文, 执行由模型按指令完成 ——
// 工具不做任何运行 (对齐 dsh: skill 是纯指令资产)。
ToolDefinition makeSkillTool();

}
