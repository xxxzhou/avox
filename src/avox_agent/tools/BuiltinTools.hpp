#pragma once

// ============================================================================
// 内置工具的统一注册入口。
//
// 取代原 ToolRegistry (Meyers 单例 + 硬编码枚举): 新架构的注册表是 AgentHost 里的
// ToolRuntime, 而「注册哪些工具」是装配决定, 应当写在一个显式的函数里而不是藏在单例的
// 首次初始化里。
//
// 注册顺序不影响发给模型的 schema 顺序 —— ToolRuntime 按名字字典序输出, 于是 KV cache
// 前缀在任何机器上都一致。
// ============================================================================

#include <string>
#include <vector>

#include "avox_agent/compose/AgentHost.hpp"
#include "avox_agent/core/AttachmentStore.hpp"

namespace avox {

// 注册全部内置工具到 host, 返回撤销器 (逆序撤销)。
//
// 包含:
//   read / read_image                分段读与图片读 (只读, 可并行)
//   grep / glob                      行定位与路径匹配 (grep 可并行; glob 按 dsh 独占)
//   write / edit                     整体写与字面替换 (独占, 原子落盘)
//   run_code / pwsh                  spawn python / PowerShell (独占)
//   todo_write                       todo 列表快照 (todo/write 事件, 独占)
//   skill                            按 name 加载一条 skill 的完整指令 (对齐 dsh, 唯一加载器)
//
// avox_cli 各命令不再逐条包成工具: 工具面过大, 每条命令的 schema 是每个 step 都要重付的
// token。命令层改由 assets/agent/skills 的 avox-cli skill 承载用法与纪律, 模型经 pwsh
// 跑 avox_cli 执行 (附带的收益: 长任务可从外部按进程树 kill,
// 不再有进程内调用卡死等超时的问题)。
//
// attachments: 附件仓 (read_image 需要; 可为空 —— 工具注册照常, 执行期报不可用)。
//
// conversationImageInput: 会话主模型是否支持图片输入 (ProviderCatalog 里该模型的
// imageInput)。纯文本主模型 (DeepSeek/glm-4-flash/glm-4.7-flash) 不认 image_url:
// 若仍注册 read_image, 模型会把 ImageBlock 喂给文本端点导致 HTTP 400。为 false 时
// 不注册 read_image, 看图改由 vision-toolkit 的 see-image 走独立视觉供应商。
//
// 对齐 dsh: 不再为每条 skill 生成一个同名工具, 也不再有 execute —— skill 目录进
// system prompt, 模型选中后用单一 skill 工具加载正文。
std::vector<Disposer> registerBuiltinTools(AgentHost& host,
                                           AttachmentStore* attachments,
                                           bool conversationImageInput);

}
