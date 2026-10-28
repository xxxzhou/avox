#pragma once

#include <stdint.h>

#include "AvoxDef.h"

namespace avox {

// 文本字幕编码名(ass/subrip/mov_text/webvtt 等)可导出为 SRT;
// 位图字幕(pgs/dvd/dvb/xsub)返回 false, 由调用方走语音识别兜底。
AVOX_EXPORT bool isTextSubtitleCodec(const char* codecName);

// SRT 写出器: 追加式接口, text 只在 add 调用内有效, 不跨 ABI 持有裸指针。
// close 时按开始时间排序后落盘; 没有有效条目返回 false。
class ISrtWriter {
 public:
  virtual ~ISrtWriter() = default;
  // 指定输出路径并清空上一次内容; 路径为 UTF-8。
  virtual bool open(const char* path) = 0;
  // 追加一条字幕; 空文本返回 false(不写入)。
  virtual bool add(int64_t startMs, int64_t endMs, const char* text) = 0;
  // 写盘并关闭; 成功返回 true。close 后可再次 open 复用。
  virtual bool close() = 0;
};

AVOX_EXPORT ISrtWriter* createSrtWriter();

// 把容器内一条文本字幕轨导出为 SRT。streamIndex 是容器 stream index
// (与 probe 字幕项的 index 一致)。返回:
//   0 成功; -1 打开/探测失败; -2 轨不存在或不是字幕; -3 位图字幕不支持;
//   -4 解码失败; -5 写出失败; -6 没有可导出的文本。
AVOX_EXPORT int32_t exportSubtitleTrack(const char* url, int32_t streamIndex,
                                        const char* outPath);

}  // namespace avox
