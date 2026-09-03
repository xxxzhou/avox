#pragma once

#include "avox_agent/core/ToolTypes.hpp"

namespace avox {

// pwsh: 执行 PowerShell 命令并捕获 stdout/stderr, 对齐 dsh 的 pwsh 契约:
// 非零退出码以 [exit code: N] 标记返回 (不算工具失败), 超时杀进程树并标
// [timed out after Nms]; 独占。
ToolDefinition makePwshTool();

}
