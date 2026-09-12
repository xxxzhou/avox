#pragma once

#include "AvoxAudio.h"
#include "AvoxBuffer.h"
#include "AvoxDef.h"
#include "AvoxVideo.h"

namespace avox {

// 音频编解码器类型(值导出给引擎插件, 只增不改不删)
#define AVOX_MAP_ACODEC(XX)     \
  XX(aac, 0, "aac")            \
  XX(g711a, 1, "PCMA")         \
  XX(g711u, 2, "PCMU")         \
  XX(opus, 3, "opus")          \
  XX(pcms16le, 4, "pcmS16LE")  \
  XX(pcms24le, 6, "pcmS24LE")  \
  XX(mp3, 7, "mp3")            \
  XX(ac3, 8, "ac3")            \
  XX(wmav1, 9, "wma1")         \
  XX(wmav2, 10, "wma2")        \
  XX(wmapro, 11, "wmapro")     \
  XX(cook, 12, "cook")         \
  XX(sipr, 13, "sipr")         \
  XX(atrac3, 14, "atrac3")     \
  XX(pcms16be, 15, "pcmS16BE")

// 视频编解码器类型(值导出给引擎插件, 只增不改不删)
#define AVOX_MAP_VCODEC(XX)  \
  XX(h264, 1, "h264")       \
  XX(h265, 2, "h265")       \
  XX(mpeg1, 3, "mpeg1")     \
  XX(mpeg2, 4, "mpeg2")     \
  XX(mpeg4, 5, "mpeg4")     \
  XX(h263, 6, "h263")       \
  XX(flv1, 7, "flv1")       \
  XX(wmv1, 8, "wmv1")       \
  XX(wmv2, 9, "wmv2")       \
  XX(wmv3, 10, "wmv3")      \
  XX(vc1, 11, "vc1")        \
  XX(rv10, 12, "rv10")      \
  XX(rv20, 13, "rv20")      \
  XX(rv30, 14, "rv30")      \
  XX(rv40, 15, "rv40")

enum class ACodecId : int32_t {
  none = -1,
#define XX(name, value, str) name = value,
  AVOX_MAP_ACODEC(XX)
#undef XX
};

// 视频编解码器类型
enum class VCodecId : int32_t {
  none = -1,
#define XX(name, value, str) name = value,
  AVOX_MAP_VCODEC(XX)
#undef XX
};

#define AVOX_MAP_TRACK_TYPE(XX) \
  XX(none, 0, "none")          \
  XX(audio, 1, "audio")        \
  XX(video, 2, "video")        \
  XX(subtitle, 3, "subtitle")

enum class TrackType {
#define XX(name, value, str) name = value,
  AVOX_MAP_TRACK_TYPE(XX)
#undef XX
};

#define AVOX_H265_TYPE(v) (((uint8_t)(v) >> 1) & 0x3f)

#define AVOX_H264_TYPE(v) ((uint8_t)(v) & 0x1F)

enum class VStreamFormat {
  other = -1,
  annexb = 0,
  // h264 avcc h265 hvcc
  vcc = 1
};

// 视频解码技术 DecodeTechnique
enum class VCodecTh : int32_t {
  other = -1,
  // 软解
  cpu,
  // Vulkan解码
  vulkan,
  // ios的VideoToolbox
  iosVT,
  // anroid的MediaCodec
  androidMC,
  // windows平台下，ffmpeg的dx11解码
  dx11
};

// 一个流里一般包含一个视频与音频包队列
// 假定可以处理多个流，流里多个trark
struct VTrackDesc {
  // 对应流Id,音频与视频可能同用一个
  int32_t trackId = 0;
  VCodecId codecId = VCodecId::none;
  // 视频信息
  VideoDesc desc = {};
};

struct ATrackDesc {
  // 对应流Id,音频与视频可能同用一个
  int32_t trackId = 0;
  ACodecId codecId = ACodecId::none;
  // 音频信息
  AudioDesc desc = {};
};

// 视频Buffer,YUV图像,可以直接转ImageBuffer使用
class IVideoBuffer {
 public:
  IVideoBuffer() = default;
  virtual ~IVideoBuffer() = default;

 public:
  virtual int32_t getWidth() = 0;
  virtual int32_t getHeight() = 0;
  // 视频源大部分返回的是YUV格式
  virtual bool bNoYUV() { return false; }
  // 如果是YUV格式，返回YUV类型
  virtual YuvType getYuvType() = 0;
  // YUV格式会转化成对应图像类型
  virtual ImageType getImageType() = 0;
};

// 与外部交互解码后的音频帧
struct AvoxAFrame {
  int64_t pts = 0;
  AvoxData buffer = {};
};

// 解码器参数(h264从SPS里获取)
struct DecoderParams {
  int32_t width = 0;
  int32_t height = 0;
  int32_t yBitDepth = 8;
  int32_t uvBitDepth = 8;
  double fps = 0;
  YuvType yuvType = YuvType::other;
};

extern "C" {
AVOX_EXPORT const char* getACodecName(ACodecId codecId);
AVOX_EXPORT const char* getVCodecName(VCodecId codecId);
}

}