#pragma once

#include "AvoxDef.h"
#include <cstring> 

namespace avox {

// CPU内存申请
class IAvBuffer {
public:
  IAvBuffer(/* args */) = default;
  virtual ~IAvBuffer() = default;

public:
  // itemCount是表示有多少个对象，itemSize表示每个对象大小
  virtual void setSize(int32_t itemCount, int32_t itemSize = 1) = 0;
  virtual int32_t size() = 0;
  virtual uint8_t *pointer() = 0;
  virtual int32_t itemCount() = 0;
  virtual int32_t itemSize() = 0;
  virtual void clear() = 0;
};

// 导出给外面的结构，bool都用int32_t替换
struct AvoxData {
  uint8_t *data = nullptr;
  int32_t size = 0;
  // 引用表示是外部项目的数据，否则表示本项目管理的空间
  int32_t bRef = false;
};

#define AVOX_MAP_PACK_TYPE(XX)                                                  \
  XX(other, 0, "other")                                                        \
  XX(video, 1, "video")                                                        \
  XX(audio, 2, "audio")                                                        \
  XX(vconfig, 3, "vconfig")                                                    \
  XX(aconfig, 4, "aconfig")                                                    \
  XX(subtitles, 5, "subtitles")

enum class PackType : int32_t {
#define XX(name, value, str) name = value,
  AVOX_MAP_PACK_TYPE(XX)
#undef XX
};

// 未解码之前的包数据
struct AvoxPacket {
  // 对应上面的PackType,视频/音频/视频配置/音频配置/字幕
  int32_t packtype = 0;
  int32_t index = 0;
  // 视频可能是关键帧 0普通非I帧,1关键帧,2-n 特殊意义,配置变化提示帧
  int32_t frameType = 0;
  // h264/h265(前3/4或是无意义的,aac前七可能是adts)
  int32_t prefixSize = 0;
  // 渲染时间 (如果包含B帧,此数据可能没有,需要使用dts)
  int64_t pts = 0;
  // 解码时间
  int64_t dts = 0;
  int32_t duration = 0;
  // 具体数据
  AvoxData data = {};
};

extern "C" {
AVOX_EXPORT bool bConfigType(PackType packType);
AVOX_EXPORT const char *getPackTypeStr(PackType packType);
}

}
