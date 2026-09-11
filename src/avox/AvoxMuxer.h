#pragma once

#include "AvoxSource.h"

namespace avox {

// 传统标准网络流解析
#define AVOX_MAP_IO_PLAN_TYPE(XX)  \
  XX(none, 0, "none")             \
  XX(zlmediakit, 1, "zlmediakit") \
  XX(ffmpeg, 2, "ffmpeg")         \
  XX(torrent, 3, "torrent")

// 传统标准网络流解析
enum class IoPlan {
#define XX(name, value, str) name = value,
  AVOX_MAP_IO_PLAN_TYPE(XX)
#undef XX
};

// 不成功，没有必要继续尝试，负值
// 0 成功
// 不成功，但是能继续尝试，正值
#define AVOX_MAP_ENCODE_RESULT(XX)     \
  XX(timeout, -100, "timeout")        \
  XX(noSupport, -4, "no support")     \
  XX(noFind, -3, "no find")           \
  XX(openFailed, -2, "open failed")   \
  XX(startFailed, -1, "start failed") \
  XX(success, 0, "success")           \
  XX(noConfig, 1, "noConfig")         \
  XX(complete, 2, "complete")         \
  XX(dataNoReady, 3, "data no ready") \
  XX(dataError, 4, "data error")

enum class EncodeResult {
#define XX(name, value, str) name = value,
  AVOX_MAP_ENCODE_RESULT(XX)
#undef XX
};

// 传统标准网络流解析
#define AVOX_MAP_MUXER_TYPE(XX)    \
  XX(none, 0, "none")             \
  XX(ffmpeg, 1, "ffmpeg")         \
  XX(zlmediakit, 2, "zlmediakit") \
  XX(onvif, 3, "onvif")

enum class MuxerType {
#define XX(name, value, str) name = value,
  AVOX_MAP_MUXER_TYPE(XX)
#undef XX
};

#define AVOX_MAP_RECORDER_STATE(XX) \
  XX(none, 0, "none")              \
  XX(opening, 1, "opening")        \
  XX(recording, 2, "recording")    \
  XX(completed, 3, "completed")    \
  XX(failed, 4, "failed")

enum class RecorderState : int16_t {
#define XX(name, value, str) name = value,
  AVOX_MAP_RECORDER_STATE(XX)
#undef XX
};

struct RecorderProgress {
  // 已处理时长(毫秒)，-1 表示不以时间计进度
  int64_t currentTimeMs = -1;
  // 总时长(毫秒)，-1 未知
  int64_t totalTimeMs = -1;
};

class IRecorderOb {
 public:
  IRecorderOb() = default;
  virtual ~IRecorderOb() = default;

 public:
  // 状态变化
  virtual void onStateChange(RecorderState preState, RecorderState state) {};
  // 进度回调
  virtual void onProgress(const RecorderProgress& progress) {};
  // IO错误返回
  virtual void onIoError(AVError error, const char* msg) {}
  // 编码错误返回
  virtual void onEncodeError(TrackType trackType, EncodeResult error) {}
  // 完成
  virtual void onComplete() {}
};

class IMediaMuxer {
 public:
  virtual ~IMediaMuxer() = default;

 public:
  // windows默认软编,注意硬编给NV12,软编给YUV420P
  virtual void setHardEncode(bool hard) {};
  // 设置视频编码 H264/H265,默认H265,设置none会关闭录制视频
  virtual void setVideoCodec(VCodecId codecId) {};
  // 设置音频编码 AAC/PCM,默认AAC,设置none会关闭录制音频
  virtual void setAudioCodec(ACodecId codecId) {};
  // 设置输出音频格式(如果不设置,不改变输入格式)
  virtual void setAudioDesc(const AudioDesc& adesc) {};
  // 用那种sdk推流,现有ffmpeg/zlmediakit
  virtual void setMuxerType(MuxerType type) = 0;
  virtual RecorderState getState() = 0;
  // 打开
  virtual bool open(const char* url) = 0;
  virtual void close() = 0;
};

// 流录制器,三个作用
// 1 bTranscode为false,直接保存/转发原始流
// 2 bTranscode为true,转录原始流,ISurfaceRender处理图像
// 3 bTranscode为true,并且outputUrl为null,用来离屏处理
class IRecorder {
 public:
  virtual ~IRecorder() = default;

 public:
  // 如果不设置,默认是ffmpeg
  virtual void setIoPlan(IoPlan ioPlan) = 0;
  // 如果不设置,默认是ffmpeg
  virtual void setMuxerType(MuxerType type) = 0;
  // 设置视频编码,设none丢弃视频轨(open前设置)
  virtual void setVideoCodec(VCodecId codecId) {};
  // 设置音频编码,设none丢弃音频轨(open前设置)
  virtual void setAudioCodec(ACodecId codecId) {};
  // 转码录制器才有的,针对图像处理
  virtual ISurfaceRender* getSurfaceRender() = 0;
  // 获取音频渲染器(用于 AudioTap 读取音频数据)
  virtual IAudioRender* getAudioRender() = 0;
  // outputUrl可以为空,空的话一般是转码离线处理数据
  // 一般用ISurfaceRender/IAudioRender获取数据并做处理
  virtual bool open(const char* inputUrl, const char* outputUrl) = 0;
  // 拉流结束及IO错误也会自动关闭
  virtual void close() = 0;
  virtual RecorderState getState() = 0;
  // seek 到相对位置(毫秒),内部自动加 basetime;
  // 未 recording/源不可 seek 返回false  
  virtual bool seek(int64_t posMs) { return false; }
  // 源总时长(毫秒),<=0 表示直播/未知(不可 seek);recording 后有效
  virtual int64_t getDuration() { return 0; }
  // 源信息(track 描述/canSeek);recording 前返回 nullptr
  virtual ISourceInfo* getSourceInfo() { return nullptr; }
  // 返回内置的参数设置器(io.*/rec.*系列key,open前设置生效)
  virtual IOption* getOption() = 0;
};

extern "C" {
// 获取录制状态字符串描述
AVOX_EXPORT const char* getRecorderStateStr(RecorderState state);
AVOX_EXPORT const char* getMuxerTypeStr(MuxerType type);
// 流录制器,bTranscode为true后会先解码,图像处理后再编码
AVOX_EXPORT IRecorder* createRecorder(bool bTranscode);
AVOX_EXPORT void addRecorderOb(IRecorder* recorder, IRecorderOb* ob);
AVOX_EXPORT void removeRecorderOb(IRecorder* recorder, IRecorderOb* ob);
// 媒体封装器观察者(IRecorderOb统一用于Muxer和Recorder)
AVOX_EXPORT void addMuxerOb(IMediaMuxer* muxer, IRecorderOb* ob);
AVOX_EXPORT void removeMuxerOb(IMediaMuxer* muxer, IRecorderOb* ob);
}

}
