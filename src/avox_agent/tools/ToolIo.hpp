#pragma once

// ============================================================================
// DSH 同名基础工具 (write/edit/glob/read_image) 的公共文件设施。
//
// 语义对齐 dsh fs-local 后端 (packages/fs/fs-local/src/fsio.ts):
//   - 编辑在 LF 规范化内容上匹配 (CRLF→LF, 孤立 \r 不动), 写回时恢复读取时探测到的
//     主流行尾 (CRLF 下先再规范化防 \r\r\n)。
//   - 写入走同目录临时文件 + 原子替换, 失败不留半截文件。
//   - 相对路径相对进程工作目录解析 —— 文件基础工具始终以进程 cwd 为基准, 不读会话
//     cwd (会话 cwd 只影响日志归置与 agent-instructions, 与文件路径解析无关)。
// ============================================================================

#include <filesystem>
#include <optional>
#include <string>

#include "avox/Avox.hpp"  // expandEnvPath

namespace avox {

// 相对路径相对进程 (= 会话) 工作目录解析; 顺带展开 %ENV% (与 read/grep 的入参处理一致)。
std::filesystem::path resolveAgentPath(const std::string& raw);

// dsh normalizeLineEndings: 仅 CRLF→LF; 孤立 \r 保留原样。
std::string normalizeLf(const std::string& text);

// dsh detectLineEndings 的判定: 前 4096 字节里 CRLF 多于裸 LF 则 CRLF。
bool checkCrlfDominant(const std::string& raw);

// dsh restoreLineEndings 的 CRLF 支: 先再规范化, 再把 \n 整体转 \r\n。
std::string restoreCrlf(const std::string& lfText);

// 读整个文件为二进制文本; 打不开 / 读不全返回 nullopt。
std::optional<std::string> readWholeFile(const std::filesystem::path& path);

// 原子写: 同目录临时文件 + 替换 (Windows 用 MoveFileExW 覆盖既有文件)。父目录不存在则
// 创建。失败返回 false, 不产生可见残留 (临时文件尽力删除)。
bool writeWholeFileAtomic(const std::filesystem::path& path, const std::string& content);

}
