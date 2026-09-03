#pragma once

// ============================================================================
// 工具结果溢出裁剪。
//
// 对齐 dsh 的 packages/spill/spill-policy。挂在 tools/post-execute 上 (prepend)。
//
// 防的具体故障: 工具结果无界地回灌模型上下文。旧实现 (已删) 有两处截断 —— notifyToolResult
// 截 4KB (展示用)、TrackRecorder 截 100 字符 (记录用) —— 偏偏漏了最该截的那个: 进
// req.messages 回灌模型的是全文, 而 read / grep / run_code 正是最容易
// 吐几 MB 的三个工具。
//
// 三个照搬的细节:
//   1. **先 next() 让下游定稿, 再对结果 bounding** —— 于是「谁替换了内容, 替换值也会被
//      裁剪」。这是它注册成 prepend 的唯一理由。
//   2. notice 自身的字节成本**先从 cap 里预扣**, 按最坏情况定价, 所以替换体永不超 cap,
//      也不会出现「裁完比原文还长」。
//   3. 落盘失败一律保留原文 + warn, **绝不把一次成功的调用变成失败**, 也不隐藏内联结果。
//
// avox 这里有个天然优势: 已经有 read, 所以溢出文件的路径直接就是可取回句柄,
// 不必另造一套取回机制。
// ============================================================================

#include <string>
#include <vector>

#include "avox_agent/core/Scope.hpp"
#include "avox_agent/core/ToolRuntime.hpp"

namespace avox {

struct SpillPolicyConfig {
  // 模型可见的纯文本结果字节上限 (UTF-8)。0 表示整个策略是空操作。
  int maxInlineBytes = 0;

  // 溢出文件的存放目录 (会按需创建)。
  std::string spillRoot;

  // 跳过的工具名。
  //
  // 读文件类工具必须在这里 —— 否则会形成 read → spill → 再 read 的循环: 模型看到
  // 「完整内容在某个文件里」, 于是又去读那个文件, 又溢出。
  std::vector<std::string> skipTools{"read"};
};

// 安装溢出策略; maxInlineBytes 为 0 时什么都不注册 (真正的空操作)。
Disposer installSpillPolicy(ToolRuntime& tools, SpillPolicyConfig config);

}
