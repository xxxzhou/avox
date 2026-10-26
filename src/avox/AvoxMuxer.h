#pragma once

#include "AvoxSource.h"

namespace avox {

// 传统标准网络流解析
#define AVOX_MAP_IO_PLAN_TYPE(XX)  \
  XX(none, 0, "none")             \
  XX(zlmediakit, 1, "zlmediakit") \
  XX(ffmpeg, 2, "ffmpeg")         \
  XX(torrent, 3, "torrent")       \
  XX(smb, 4, "smb")               \
  XX(dav, 5, "dav")               \
  XX(disc, 6, "disc")

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
  XX(failed, 4, "failed")          \
  XX(seeking, 5, "seeking")

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

// 编码用途档(内部概念, 不是用户旋钮): 只决定"用户没显式指定时"各旋钮的缺省取向。
// record = 转码/录制落盘(体积与画质优先); stream = 实时推流(码率硬约束, 低延迟优先)
enum class EncodeUsage : int32_t {
  record = 0,
  stream = 1,
};

// 码率控制模式
#define AVOX_MAP_ENCODE_RATE_MODE(XX) \
  XX(inherit, -1, "inherit")          \
  XX(quality, 0, "quality")           \
  XX(vbr, 1, "vbr")                   \
  XX(cbr, 2, "cbr")

enum class EncodeRateMode : int32_t {
#define XX(name, value, str) name = value,
  AVOX_MAP_ENCODE_RATE_MODE(XX)
#undef XX
};

// 编码预设(速度↔质量)
#define AVOX_MAP_ENCODE_PRESET(XX) \
  XX(inherit, -1, "inherit")       \
  XX(speed, 0, "speed")            \
  XX(balanced, 1, "balanced")      \
  XX(quality, 2, "quality")

enum class EncodePreset : int32_t {
#define XX(name, value, str) name = value,
  AVOX_MAP_ENCODE_PRESET(XX)
#undef XX
};

// 编码 profile(兼容性↔编码效率)
#define AVOX_MAP_ENCODE_PROFILE(XX) \
  XX(inherit, -1, "inherit")        \
  XX(compat, 0, "compat")           \
  XX(main, 1, "main")               \
  XX(high, 2, "high")

enum class EncodeProfile : int32_t {
#define XX(name, value, str) name = value,
  AVOX_MAP_ENCODE_PROFILE(XX)
#undef XX
};

// 视频编码参数(录制器常用的一组旋钮, open 前设置生效)。
// 逐项独立: 只改想改的那项, 其余留缺省; 缺省 = 按录制用途档推导
// (质量优先 + 质量档60 + 均衡预设 + high profile + 3秒关键帧)。
// 各编码器量纲不同(crf/cq/qp/Quality), SDK 按平台换算并实测校准
struct VideoEncodeParam {
  // 码率控制: quality=质量定档(体积随内容起伏) vbr=可变码率 cbr=恒定码率;
  // inherit = 缺省(质量优先)
  EncodeRateMode rateMode = EncodeRateMode::inherit;
  // 质量档 0..100(越大越清晰、体积越大); -1 = 缺省(60)
  int32_t quality = -1;
  // 目标码率 kbps; 0 = 按分辨率/帧率自动。给了值就按码率收口(VBR),
  // 除非另行指定 rateMode
  int32_t bitrateKbps = 0;
  // 编码预设; inherit = 缺省(均衡)
  EncodePreset preset = EncodePreset::inherit;
  // 关键帧间隔秒(seek 粒度与体积的折中); 0 = 缺省(3)。
  // 不常用, 也可经 option "rec.encode.gop" 设置
  int32_t gopSeconds = 0;
  // profile; inherit = 缺省(录制取 high, 推流取 compat)。
  // 不常用, 也可经 option "rec.encode.profile" 设置
  EncodeProfile profile = EncodeProfile::inherit;
  // 用途档: 上面各项取 inherit 时按此档推导(record=质量优先 / stream=低延迟)。
  // ⚠️ 末尾追加字段: 旧宿主编的旧结构体(少 4 字节)配新库会越界读, 必须与
  // avox 同批部署(含 SWIG 绑定重新生成); 缺省 record = 存量行为
  EncodeUsage usage = EncodeUsage::record;
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
  // 转码腿: 声明输出视频描述(与 IRecorder::setVideoDesc 同名; open前设置)。
  // 尺寸是**精确目标**不是上限(width/height 任一为 0 = 不缩尺寸): 实际像素由
  // 渲染链 ISurfaceRender::enableSizeChange 缩, 当前只有图像处理链在用的 VK
  // 车道支持(原生 DX11/EGL/Metal 腿不支持); 缩放接线在 MediaPlayer/SourcePlayer
  // 录制腿(IRecorder/TranscodeRecorder 路径只应用尺寸)。fps>0 时按 pts 丢帧并
  // 同步给编码器(fps 门仅本接口生效); fps=0 不限制。
  // 仅 RawMuxer(转码)生效, 直拷 muxer 忽略并告警
  virtual void setVideoDesc(const VideoDesc& desc) {};
  // 转码腿: 编码旋钮包(与 IRecorder::setVideoEncodeParam 同义; open前设置;
  // 用途档 usage 在结构体内, stream=低延迟推流取向)。仅 RawMuxer(转码)生效
  virtual void setVideoEncodeParam(const VideoEncodeParam& param) {};
};

// 轨道处理方式(转码录制器, open前设置; 非转码录制器本就全直拷, 不适用)
// 与 setVideoCodec/setAudioCodec 的 none=丢轨正交: none 决定轨道是否存在,
// 本枚举决定存在的轨道"转码"还是"直拷"; 直拷轨上的编码/格式设置被忽略
enum class TransMode : int32_t {
  TranscodeAll = 0,  // 视频转码 + 音频转码(默认, 存量行为不变)
  AudioCopy    = 1,  // 视频转码 + 音频直拷: 源音频包不解码不重编直接进封装
                     // (pts 同轴; 目标容器不可容该编码时自动回退转码 —— 判定在
                     //  AMediaSource::onReady 经注入的 audioCopySupported 问
                     //  muxer 实现层 IOMuxer::canStoreAudio, 该轨改走转码)
  VideoCopy    = 2   // 视频直拷 + 音频转码(转封装+改音频等场景)
};

// 流录制器,三个作用
// 1 bTranscode为false,直接保存/转发原始流
// 2 bTranscode为true,转录原始流,ISurfaceRender处理图像
class IRecorder {
 public:
  virtual ~IRecorder() = default;

 public:
  // 如果不设置,默认是ffmpeg
  virtual void setIoPlan(IoPlan ioPlan) = 0;
  // 如果不设置,默认是ffmpeg
  virtual void setMuxerType(MuxerType type) = 0;
  // 设置视频编码,设none丢弃视频轨(open前设置)
  // 仅视频为转码态(TranscodeAll/AudioCopy)时生效; VideoCopy下被忽略
  virtual void setVideoCodec(VCodecId codecId) {};
  // 设置音频编码,设none丢弃音频轨(open前设置)
  // 仅音频为转码态(TranscodeAll/VideoCopy)时生效; AudioCopy下被忽略
  virtual void setAudioCodec(ACodecId codecId) {};
  // 轨道处理方式(open前设置); 双轨全直拷即 createRecorder(false), 不设枚举值
  virtual void setTransMode(TransMode mode) {};
  // 转码录制器: 声明输出视频描述(尺寸变化/超分输出, onReady 据此改编码器描述, open前设置)
  // 转码态专用; VideoCopy下无意义被忽略
  virtual void setVideoDesc(const VideoDesc& desc) {};
  // 转码录制器: 声明输出音频格式(重采样目标, open前设置)
  // 转码态专用; AudioCopy下无意义被忽略
  virtual void setAudioDesc(const AudioDesc& desc) {};
  // 离线画质增强(Real-ESRGAN, 转码录制器专用; 队列消费侧逐帧推理, 队列满反压解码;
  // 与实时轨 ISurfaceRender::enableQualityEnhance 图内层无关, open前设置)
  virtual void enableQualityEnhance(const QualityEnhanceParamet& paramet) {};
  virtual void disableQualityEnhance() {};
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
  // 视频编码参数(转码录制器专用, open 前设置; 转封装录制器不编码, 忽略)。
  // 常用旋钮(码控模式/质量/码率/预设)只走本接口 —— 上层 UI 直接填结构体,
  // 不必拼 key 字符串; 只有不常用的关键帧间隔/profile 另有 option 入口
  // (rec.encode.gop / rec.encode.profile, 异步/远程配置用)。
  // ⚠️ 新虚函数只追加在接口末尾(往中间插会移动既有槽位, 旧二进制静默调错)
  virtual void setVideoEncodeParam(const VideoEncodeParam& param) {};
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
