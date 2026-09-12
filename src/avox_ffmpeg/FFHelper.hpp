#pragma once

#include "FFCommon.hpp"
#include <memory>

namespace avox {

#define AVOX_FFMEPG_LOG(ret, ...)                                               \
  if (ret < 0) {                                                               \
    char error_char[AV_ERROR_MAX_STRING_SIZE];                                 \
    const char *error =                                                        \
        av_make_error_string(error_char, AV_ERROR_MAX_STRING_SIZE, ret);       \
    LOGFLF(LogLevel::warn, __VA_ARGS__, " error[", ret, "]: ", error, " ");               \
  }

#define AVOX_FFMEPG_LOG_RETURN(ret, result, ...)                                \
  if (ret < 0) {                                                               \
    char error_char[AV_ERROR_MAX_STRING_SIZE];                                 \
    const char *error =                                                        \
        av_make_error_string(error_char, AV_ERROR_MAX_STRING_SIZE, ret);       \
    LOGFLF(LogLevel::warn, __VA_ARGS__, " error[", ret, "]: ", error, " ");               \
    return result;                                                             \
  }

#define AVOX_FFMEPG_LOG_RETURN_FLASE(ret, ...)                                  \
  AVOX_FFMEPG_LOG_RETURN(ret, false, __VA_ARGS__)

// 导出给插件(如avox_torrent)复用: 同工程同编译器, 无ABI顾虑
AVOX_EXPORT AVError ffIoError(int err);

AVOX_EXPORT VCodecId ffVCodec(AVCodecID codecId);
AVOX_EXPORT ACodecId ffACodec(AVCodecID codecId);
// AVSampleFormat
AVOX_EXPORT YuvType ffYuvType(AVPixelFormat format);
// 流色彩空间(矩阵标准+量程): 容器/VUI 标记优先, 未标记按分辨率(>=720p=709)与编码族(H264/H265/MPEG=limited)惯例推断
AVOX_EXPORT ColorSpaceDesc ffColorSpace(AVCodecParameters* par);
AVOX_EXPORT AudioFormat ffAudioFromat(int32_t audioFormat);
// 流帧率: codecpar->framerate 对mpeg4-in-AVI等常给脏值(如6000+),
// 越界(>480)时退 avg_frame_rate, 仍非法返回0(未知)
AVOX_EXPORT double ffFps(const AVStream* st);
AVOX_EXPORT AvoxPacket ffAvoxPacket(AVPacket *packet);
AVOX_EXPORT AVPixelFormat getFFVideoFormat(YuvType type);

AVOX_EXPORT AVCodecID getFFCodecId(VCodecId codecId);
AVOX_EXPORT AVCodecID getFFCodecId(ACodecId codecId);
AVOX_EXPORT AVSampleFormat getFFAudioFormat(AudioFormat format);

std::string getUrlProtocol(const char* url);

void ffAudioFrame(AvoxAFrame &frame, AVFrame *avFrame);

template <typename T> inline void freefobj(T *val) { av_free(val); }

// 显式具体化 对应各个具体实现
template <> inline void freefobj(AVCodecContext *val) {
  // 释放硬件设备上下文
  // if (val->hw_device_ctx) {
  //   av_buffer_unref(&val->hw_device_ctx);
  // }
  avcodec_free_context(&val);
}

template <>
inline void freefobj(AVFormatContext *val) {
  // 编码上下文（输出）
  if (val->oformat) {
    if (!(val->oformat->flags & AVFMT_NOFILE) && val->pb) {
      avio_close(val->pb);
      val->pb = nullptr;
    }
    // 不立即释放 val，继续处理解码上下文
  }
  // 解码上下文（输入）
  if (val->iformat) {
    avformat_close_input(&val);
    return;  // avformat_close_input 会释放 val
  }

  // 如果只有编码上下文，手动释放
  if (val->oformat) {
    avformat_free_context(val);
  }
}

template <> inline void freefobj(AVFrame *val) { av_frame_free(&val); }

template <> inline void freefobj(AVPacket *val) { av_packet_free(&val); }

template <> inline void freefobj(SwrContext *val) { 
  if(swr_is_initialized(val) > 0){
    swr_close(val);
  }
  swr_free(&val); }

template <> inline void freefobj(AVIOContext *val) {
  av_freep(&val->buffer);
  av_free(val);
}

template <> inline void freefobj(AVBSFContext *val) { av_bsf_free(&val); }

template <> inline void freefobj(AVCodecParameters *val) {
  avcodec_parameters_free(&val);
}

#define AVOX_UNIQUE_FUNCTION(CLASSTYPE)                                         \
  typedef void (*free##CLASSTYPE)(CLASSTYPE *);

// ffmpeg对应的智能指针
#define AVOX_UNIQUE_FCLASS(CLASSTYPE)                                           \
  typedef std::unique_ptr<CLASSTYPE, std::function<void(CLASSTYPE *)>>         \
      CLASSTYPE##Ptr;                                                          \
  inline CLASSTYPE##Ptr getUniquePtr(CLASSTYPE *ptr) {                         \
    CLASSTYPE##Ptr uptr(ptr, freefobj<CLASSTYPE>);                             \
    return uptr;                                                               \
  }

AVOX_UNIQUE_FCLASS(AVFormatContext)
AVOX_UNIQUE_FCLASS(AVCodecContext)
AVOX_UNIQUE_FCLASS(AVFrame)
AVOX_UNIQUE_FCLASS(AVPacket)
AVOX_UNIQUE_FCLASS(SwrContext)
AVOX_UNIQUE_FCLASS(AVIOContext)
AVOX_UNIQUE_FCLASS(AVBSFContext)
AVOX_UNIQUE_FCLASS(AVCodecParameters)

}
