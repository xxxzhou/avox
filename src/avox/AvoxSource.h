#pragma once

#include "AvoxAudio.h"
#include "AvoxBuffer.h"
#include "AvoxCodec.h"
#include "AvoxLayer.h"
#include "AvoxBase.h"
#include "AvoxVideo.h"

namespace avox {

#define AVOX_MAP_AV_ERROR(XX)              \
  XX(deviceClose, -8, "device close")     \
  XX(deviceDisable, -7, "devcie disable") \
  XX(deviceError, -6, "devcie error")     \
  XX(deviceBusy, -5, "devcie busy")       \
  XX(devcieLost, -4, "devcie lost")       \
  XX(netShutdown, -3, "net shutdown")     \
  XX(netTimeout, -2, "net timeout")       \
  XX(urlNoSupport, -1, "url no support")  \
  XX(none, 0, "none")                     \
  XX(endOfFile, 1, "end of file")         \
  XX(sourceNoVaild, 2, "source no vaild") \
  XX(dataNoVaild, 3, "data no vaild")     \
  XX(other, 100, "other")

// 0表示无错误,负一般表示中断性错误，正表示可继续
// 相应Source可能需要具体分析
enum class AVError {
#define XX(name, value, str) name = value,
  AVOX_MAP_AV_ERROR(XX)
#undef XX
};

// 本地播放源,下载源(HLS,Http),直播源(RTSP/RTSP)
#define AVOX_MAP_AV_SOURCE_MODE(XX) \
  XX(none, 0, "none")              \
  XX(local, 1, "local")            \
  XX(live, 2, "live")              \
  XX(downLive, 3, "downLive")

// 本地播放源,下载源(HLS,Http),直播源(RTSP/RTSP),回放源(RTSP)
enum class AVSourceMode {
#define XX(name, value, str) name = value,
  AVOX_MAP_AV_SOURCE_MODE(XX)
#undef XX
};

// Raw源类型，现在有设备与WebRTC两种
#define AVOX_MAP_RAW_SOURCE_TYPE(XX) \
  XX(none, 0, "none")               \
  XX(device, 1, "device")           \
  XX(media, 2, "media")             \
  XX(WebRTC, 3, "WebRTC")

// IO源类型，如标准协议需要解码，WebRTC自身调用解码，YUV数据直出
enum class RawSourceType {
#define XX(name, value, str) name = value,
  AVOX_MAP_RAW_SOURCE_TYPE(XX)
#undef XX
};

// seek类型与操作应该是由IO层定
enum class SeekType {
  none = 0,
  // 有总时长
  normal,
  // 不一定有总时长
  time,
};

enum class VBufferType { cpu, vulkan, dx11, opengles, metal };

struct GpuFrame {
  int64_t pts = 0;
  int64_t dts = 0;
  int32_t keyFrame = 0;
  // 没太大意义,原始硬解出来的几乎都是NV12,而在显示层都是RGBA
  YUVFormat format = {};
  int64_t queueIndex = 0;
  IRenderContext* context = nullptr;
  void* buffer = nullptr;
};

// 设备类型
enum class DeviceType {
  none = 0,
  audio,
  video,
};

// 视频设备来源类别
enum class VDeviceKind {
  none = 0,
  camera,   // 相机
  window,   // 窗口
  monitor,  // 显示器
};

// 音频设备来源类别
enum class ADeviceKind {
  none = 0,
  mic,       // 麦克风
  loopback,  // 系统回环采集(声卡输出)
};

enum class ADeviceSdk { none = 0, wasapi, android, ios };

enum class VDeviceSdk {
  none = 0,
  win_mf,
  win_capture,
  and_ndkcamer2,
  ios_avf,
  win_decklink,
};

class IAudioSourceOb {
 public:
  virtual ~IAudioSourceOb() = default;

 public:
  virtual void onAudioDesc(const AudioDesc& desc) {};
  virtual void onAudioError(AVError error, const char* msg) {};
  virtual void onAudioFrame(const AvoxAFrame& frame) {};
  virtual void onAudioClose() {}
};

class IVideoSourceOb {
 public:
  virtual ~IVideoSourceOb() = default;

 public:
  // 一般来说实现是第一帧数据到时，把帧数据的描述信息
  virtual void onVideoDesc(const VideoDesc& desc) {};
  virtual void onVideoError(AVError error, const char* msg) {};
  // CPU视频帧
  virtual void onVideoFrame(const YUVFrame& frame) {};
  // GPU视频帧
  virtual void onGpuFrame(const GpuFrame& frame) {};
  virtual void onVideoClose() {}
};

class IVideoSource {
 public:
  virtual ~IVideoSource() = default;

 public:
  // 显示名
  virtual const char* getDeviceName() = 0;
  // 标示Id
  virtual const char* getDeviceId() = 0;
  // 打开
  virtual bool open() = 0;
  // 关闭
  virtual void close() {};
  // 查询是否打开
  virtual bool bOpening() = 0;
  // 设备来源类别
  virtual VDeviceKind getDeviceKind() = 0;
};

class IAudioSource {
 public:
  virtual ~IAudioSource() = default;

 public:
  virtual const char* getDeviceName() = 0;
  virtual const char* getDeviceId() = 0;
  // 打开
  virtual bool open() = 0;
  // 关闭
  virtual void close() {};
  // 查询是否打开
  virtual bool bOpening() = 0;
  // 设备来源类别
  virtual ADeviceKind getDeviceKind() = 0;
};

class IVideoManager {
 public:
  virtual ~IVideoManager() = default;

 public:
  virtual IVideoSource* getDevice(int32_t index) = 0;
  virtual IVideoSource* findDevice(const char* id) = 0;
  virtual int32_t getDeviceCount() = 0;
  // virtual const char* getDeviceName(int32_t index) = 0;
  // 刷新设备列表，注意刷新可能让外部IDeviceSource的指针失效
  // 需要确保外部不在使用刷新前的IDeviceSource,否则别用
  virtual void refreshDevices() {};
};

class IAudioManager {
 public:
  virtual ~IAudioManager() = default;

 public:
  virtual IAudioSource* getDevice(int32_t index) = 0;
  virtual IAudioSource* findDevice(const char* id) = 0;
  virtual int32_t getDeviceCount() = 0;
  // virtual const char* getDeviceName(int32_t index) = 0;
  // 刷新设备列表，注意刷新可能让外部IDeviceSource的指针失效
  // 需要确保外部不在使用刷新前的IDeviceSource,否则别用
  virtual void refreshDevices() {};
};

// 源信息，包含是否有音频与视频，及音频与视频的track信息
// 请在相应player里的onReady回调里调用,这时这个对象是一定在的
class ISourceInfo {
 public:
  virtual ~ISourceInfo() = default;

 public:
  virtual int32_t videoSize() = 0;
  virtual int32_t audioSize() = 0;
  virtual VTrackDesc getVideoDesc(int32_t index) = 0;
  virtual ATrackDesc getAudioDesc(int32_t index) = 0;
  // 是否可以seek
  virtual bool canSeek() = 0;
};

// 编码数据源，如H264/H265/AAC
// 如果是从IMediaPlayer里得到的,请不要直接调用open/close
// 请调用IMediaPlayer::open/close
class IAVSource {
 public:
  IAVSource() = default;
  virtual ~IAVSource() = default;

 public:
  // 打开URL
  virtual bool open(const char* url) = 0;
  // 关闭
  virtual void close() = 0;
  // 暂停
  virtual void pause(bool bFlag) {};
  // 在调用open，回调onOpen后能获取到正确信息
  virtual ISourceInfo* getSourceInfo() = 0;
  // 能seek吗?
  virtual SeekType seekType() const { return SeekType::none; }
  // 0-1
  virtual void seekTo(double progress) {};
  // 毫秒时间
  virtual bool seekTo(int64_t pos) { return false; };
  // 返回总时长，单位毫秒
  virtual int64_t duration() const { return 0; }
  // 返回进度，0~1
  virtual double progress() const { return 0; }
  // 返回相对起始时间的播放时间，单位毫秒
  virtual int64_t position() const { return 0; }
};

class IRawSourceOb {
 public:
  IRawSourceOb() = default;
  virtual ~IRawSourceOb() = default;

 public:
  // 音频与视频描述信息都准备好后回调
  virtual void onReady() {};
  virtual void onClose() {};
  virtual void onError(AVError error, const char* msg) {}
  // 返回数据
  virtual void onVideoFrame(const YUVFrame& frame, int32_t trackId) {};
  // GPU视频帧
  virtual void onGpuFrame(const GpuFrame& frame, int32_t trackId) {};
  // 音频帧
  virtual void onAudioFrame(const AvoxAFrame& frame, int32_t trackId) {};
};

// 原始数据源，如YUV/GPUTexture/PCM
// 回调是IRawSourceOb
// 如果是从ISourcePlayer里得到的,请不要直接调用open/close
// 请调用ISourcePlayer::open/close
class IRawSource {
 public:
  IRawSource() = default;
  virtual ~IRawSource() = default;

 public:
  virtual bool open() = 0;
  virtual void close() = 0;
  // 查询是否打开
  virtual bool bOpening() = 0;
  // 在调用open，回调onOpen后能获取到正确信息
  virtual ISourceInfo* getSourceInfo() = 0;
};

extern "C" {
// 工具函数：路径和文件检测
// 检测是否是本地路径（非网络URL）
AVOX_EXPORT bool checkLocalPath(const char* url);
AVOX_EXPORT const char* getAVErrorStr(AVError error);
AVOX_EXPORT const char* getAVSoureceModeStr(AVSourceMode mode);
AVOX_EXPORT const char* getRawSourceTypeStr(RawSourceType type);
// 获取视频设备管理器，根据sdk获取不同设备管理器
AVOX_EXPORT IVideoManager* getVideoManager(VDeviceSdk sdk);
// 当前平台的默认视频设备 SDK (每平台只实现一个: Win=win_capture, Android=and_ndkcamer2, iOS=ios_avf)
AVOX_EXPORT VDeviceSdk getDefaltVideoSdk();
AVOX_EXPORT ADeviceSdk getDefaltAudioSdk();
// 获取音频设备管理器，根据sdk获取不同设备管理器
AVOX_EXPORT IAudioManager* getAudioManager(ADeviceSdk sdk);
// 获取当前进程消耗的物理内存(单位:KB)
AVOX_EXPORT uint64_t getCurrentMemoryUsageKB();
}

}
