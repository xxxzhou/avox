#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

#include "../AvoxCodec.h"

namespace avox {

// 严格 UTF-8 校验: 拒绝孤立续字节 / 过长编码 / 代理区 / 超范围码点。
// 纯逻辑无平台依赖, 供编码嗅探与单测使用。
bool isUtf8Text(const char* data, size_t size);

// 字幕外挂文本编码归一, 产出可直接进入解析与光栅化的 UTF-8, 并返回探测结果:
// utf8            已是 UTF-8(无 BOM)原样返回(最常见路径)
SubtitleEncoding normalizeSubtitleText(const std::string& raw, std::string& out);

// UTF-8 路径打开文件(路径编码自愈): Windows 下 UTF-8→UTF-16 走 _wfopen(与
// ACP 无关, 中文名路径可开), 其余平台 UTF-8 即原生窄字符直接 fopen。核心
FILE* openFileUtf8(const char* path, const char* mode);

#if defined(_WIN32)
// UTF-8 → UTF-16(MSVC 的 ifstream 直收宽路径用); 仅 Windows 提供
std::wstring utf8ToWide(const char* s);
#endif

}  // namespace avox
