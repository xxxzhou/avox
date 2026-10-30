#include "avox/AvoxMedia.h"

#include "avox/AvoxCapture.h"

namespace avox {

namespace {

// AVOX_MAP_* 的值连续, 直接当字符串表下标; 越界返回 unknown
const char* mapStr(const char* const* table, int32_t count, int32_t value) {
  if (value < 0 || value >= count) {
    return "unknown";
  }
  return table[value];
}

}  // namespace

const char* getMediaOpStr(MediaOp op) {
  static const char* kStrs[] = {
#define XX(name, value, str) str,
      AVOX_MAP_MEDIA_OP(XX)
#undef XX
  };
  return mapStr(kStrs, (int32_t)(sizeof(kStrs) / sizeof(kStrs[0])), (int32_t)op);
}

const char* getMediaReaderStateStr(MediaReaderState state) {
  static const char* kStrs[] = {
#define XX(name, value, str) str,
      AVOX_MAP_MEDIA_READER_STATE(XX)
#undef XX
  };
  return mapStr(kStrs, (int32_t)(sizeof(kStrs) / sizeof(kStrs[0])),
                (int32_t)state);
}

const char* getMediaErrorStr(MediaError error) {
  static const char* kStrs[] = {
#define XX(name, value, str) str,
      AVOX_MAP_MEDIA_ERROR(XX)
#undef XX
  };
  return mapStr(kStrs, (int32_t)(sizeof(kStrs) / sizeof(kStrs[0])),
                (int32_t)error);
}

#if !defined(AVOX_ENABLE_FFMPEG)
// 无 FFmpeg 构建: 核心侧 stub(有 FFmpeg 时实现在 avox_ffmpeg/MediaReaderFF.cpp)
IMediaReader* createMediaReader() {
  return nullptr;
}

// 混音/封装同样依赖 FFmpeg 编码与 muxer
IMediaComposer* createMediaComposer() {
  return nullptr;
}
#endif

}  // namespace avox
