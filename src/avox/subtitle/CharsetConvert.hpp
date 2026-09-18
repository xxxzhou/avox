#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "../AvoxCodec.h"

namespace avox {

// 严格 UTF-8 校验: 拒绝孤立续字节 / 过长编码 / 代理区 / 超范围码点。
// 纯逻辑无平台依赖, 供编码嗅探与单测使用。
bool isUtf8Text(const char* data, size_t size);

// 字幕外挂文本编码归一, 产出可直接进入解析与光栅化的 UTF-8, 并返回探测结果:
//   utf8            已是 UTF-8(无 BOM)原样返回(最常见路径)
//   utf8BomStripped 已是 UTF-8, 剥 BOM 后返回
//   gbkTranscoded   非 UTF-8 且能按 GBK/GB18030 解出, 已转 UTF-8
//   utf16Raw        UTF-16 BOM(FF FE / FE FF), 原样返回(不误判成 GBK)
//   unknown         空输入, 或平台无转换能力/非合法 GBK(原样返回, 上层报错)
// 枚举取代旧 bool: 旧返回值把「UTF-16 原样」与「本来就是 UTF-8」混在 false 里,
// 产品无法区分「文件就是 UTF-8」与「UTF-16 未处理」。
//
// 平台能力: Windows = MultiByteToWideChar(CP936); Apple = CoreFoundation
// GB18030; 其它 = iconv(无 <iconv.h> 时降级为不转换, 如部分 Android NDK)。
SubtitleEncoding normalizeSubtitleText(const std::string& raw, std::string& out);

}  // namespace avox
