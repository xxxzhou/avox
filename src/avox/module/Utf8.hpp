#pragma once

// ============================================================================
// 文本编码卫生: 来源不可控的字节进程序内文本前, 从这里洗净成合法 UTF-8。
//
// 背景: 中文 Windows 上本地文件/子进程输出常是 GBK(ANSI 936), 按字节原样读入后若
// 直接当 UTF-8 用 (拼 JSON、发 HTTP、落盘), 轻则乱码重则整个负载非法 —— 服务端
// JSON/UTF-8 校验一律 400 且每次必现, 落盘文件则变成混编码 (avox_agent 的会话日志、
// dsh 的 checkRootEncoding 都栽过)。
//
// 与 Avox.cpp 的 utf8TWstring/utf8TString 互补: 那对做已知编码的确定性转换,
// 这组做「来源不明字节」的校验与修复。
// ============================================================================

#include <string>

#include "avox/AvoxDef.h"

namespace avox {

// 整串是否合法 UTF-8 (拒绝 overlong / 代理区 / >U+10FFFF)。
bool isValidUtf8(const std::string& text);

// 只把非法序列替换成 U+FFFD, 其余字节原样保留。用于「整体已合法的文本里混进零星
// 脏字节」的修补 —— 如已序列化的 JSON 行: Json::dump 会转义全部结构性字符, 非法
// 字节只可能出现在字符串字面量内, 修补不会碰坏 JSON 结构。
std::string patchInvalidUtf8(const std::string& text);

// 外部文本的整体卫生: 合法 UTF-8 直通; 否则视为本机 ANSI (中文 Windows = GBK/936)
// 整体转码; 仍转不动的字节变 U+FFFD。用于工具输出这类「单一来源、整体同编码」的文本。
std::string ensureUtf8(const std::string& text);

}
