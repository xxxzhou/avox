#pragma once

#include "avox_agent/core/ToolTypes.hpp"

namespace avox {

// run_code: 执行代码 (spawn 机器 python 跑代码/脚本, 可 import avox 调截图/OCR/输入/播放)。
// 对齐 dsh 的 run_code 契约: 必填 code + description。
//
// 独占且预算最长 (脚本可能做 GUI 自动化或长时间采集): GUI 焦点是全局独占资源。
ToolDefinition makeRunCodeTool();

}