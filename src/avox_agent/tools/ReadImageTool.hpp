#pragma once

#include "avox_agent/core/AttachmentStore.hpp"
#include "avox_agent/core/ToolTypes.hpp"

namespace avox {

// read_image: 读图片文件进附件仓并产出 ImageBlock, 对齐 dsh 的 read_image 契约:
// 只收 PNG/JPEG/WebP/GIF 路径, 返回 <path>/<type>/<content> 信封 + 图片块; 可并行。
//
// attachments: 附件仓接缝 (compose 注入); 为空时工具在执行期报错 —— 注册期不炸装配。
ToolDefinition makeReadImageTool(AttachmentStore* attachments);

}
