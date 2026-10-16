#pragma once

#include "avox_agent/core/ToolTypes.hpp"

namespace avox {

// glob: 按 glob 模式找文件路径, 对齐 dsh 的 glob 契约 (ripgrep --files 语义):
// 只返回文件、含隐藏文件、排除 .git 等版本库目录、按修改时间升序、上限 100 条。
ToolDefinition makeGlobTool();

}
