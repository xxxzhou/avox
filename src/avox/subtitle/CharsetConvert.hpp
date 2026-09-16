#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace avox {

// 严格 UTF-8 校验: 拒绝孤立续字节 / 过长编码 / 代理区 / 超范围码点。
// 纯逻辑无平台依赖, 供编码嗅探与单测使用。
bool isUtf8Text(const char* data, size_t size);

// 字幕外挂文本编码归一, 产出可直接进入解析与光栅化的 UTF-8:
//   已是 UTF-8(含可选 BOM) → 剥 BOM 原样返回(最常见路径)
//   非 UTF-8 且能按 GBK/GB18030 解出    → 转 UTF-8
//   UTF-16 BOM(FF FE / FE FF) 或平台无转换能力 → 原样返回(不误判成 GBK)
// 返回是否发生了转码(调用方可据此记日志)。
//
// 平台能力: Windows = MultiByteToWideChar(CP936); Apple = CoreFoundation
// GB18030; 其它 = iconv(无 <iconv.h> 时降级为不转换, 如部分 Android NDK)。
bool normalizeSubtitleText(const std::string& raw, std::string& out);

}  // namespace avox
