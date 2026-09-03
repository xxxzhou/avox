#pragma once

// ============================================================================
// dsh 的会话日志目录布局 (session-persistence-jsonl/src/format.ts 的移植)。
//
//   <root>/<projectKey(cwd)>/<encodeSegment(id)>/session.jsonl
//
// 布局是两边互访的硬约束: 同一 id 的日志必须落在逐字节相同的路径上, 否则一边写
// 一边找不到。所以这里逐函数照抄 dsh 的算法 (含 ~XXXX 转义与特判), 不做本地化。
//
// avox 全明文 (用户决策): 只认 session.jsonl, 后缀永远不带 .zstd。
// ============================================================================

#include <optional>
#include <string>

#include "SessionTypes.hpp"

namespace avox {

// 把任意字符串编成单个安全路径段 (format.ts encodeSegment)。
//
//   "."   -> "~002E"      ".."  -> "~002E~002E"     (防目录穿越)
//   安全字符 [A-Za-z0-9._-] 直通 (但 '~' 本身不安全)
//   其余按 UTF-16 码单元 "~XXXX" (大写 hex, 4 位补零)
//
// 转义按 UTF-16 码单元而不是 UTF-8 字节: dsh 在 JS 字符串上逐 charCodeAt 处理,
// 输入先解码成码单元 (含代理对拆分) 才能逐字节一致。空串抛异常 (dsh 同款)。
std::string dshEncodeSegment(const std::string& raw);

// 项目目录键 (format.ts projectKey): '/','\',':' 折叠成单个 '-', 其余同上转义,
// 去首部 '-', 空 -> "root", 最后 "--<slug 前 251 字符>--"。键是纯 ASCII (转义后),
// 截断按字符即按码单元, 与 dsh 的 slice(0, 251) 一致。空串抛异常。
std::string dshProjectKey(const std::string& cwd);

// 项目目录: cwd 缺失 -> <root>/_no-cwd (dsh 对无 cwd 会话的归置)。
std::string dshProjectDir(const std::string& root,
                          const std::optional<std::string>& cwd);

// 单会话目录: <projectDir>/<encodeSegment(id)>。
std::string dshSessionDir(const std::string& root,
                          const std::optional<std::string>& cwd,
                          const SessionId& id);

// 会话日志路径 (明文): <sessionDir>/session.jsonl。
std::string dshSessionLogPath(const std::string& root,
                              const std::optional<std::string>& cwd,
                              const SessionId& id);

// 当前工作目录的 UTF-8 表示。
//
// Windows 下 current_path().string() 给的是 ACP 字节 (中文系统是 GBK), 写进 JSON
// 日志后 dsh (Node, UTF-8) 读出来是乱码, projectKey 也会对不上; u8string() 恒 UTF-8。
std::string currentPathUtf8();

}
