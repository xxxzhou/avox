#pragma once

#include "AvoxMuxer.h"
#include "AvoxSource.h"

namespace avox {

// ============== 字幕 (SRT / ASR / 翻译) ==============
// ASR 工作模式
enum class AsrMode {
  streaming,  // 流式模式：实时显示
  ptsSync     // PTS同步模式：队列查找
};

// 字幕视图接口
class ISubtitle {
 public:
  virtual ~ISubtitle() = default;
  // 字幕文件
  virtual bool loadSrt(const char* path) = 0;
  // ASR 控制
  virtual void enableAsr() = 0;
  // 翻译
  virtual void enableTranslation() = 0;
  virtual void disableTranslation() = 0;
  // 关闭加载的字幕或是ASR
  virtual void close() = 0;
};

#define AVOX_MAP_PLAYER_STATE(XX) \
  XX(none, 0, "none")            \
  XX(opening, 1, "opening")      \
  XX(ready, 2, "ready")          \
  XX(playing, 3, "playing")      \
  XX(pause, 4, "pause")          \
  XX(seek, 5, "seek")            \
  XX(buffering, 6, "buffering")  \
  XX(stopped, 7, "stopped")      \
  XX(completed, 8, "completed")

enum class PlayerState {
#define XX(name, value, str) name = value,
  AVOX_MAP_PLAYER_STATE(XX)
#undef XX
};

// 不成功，没有必要继续尝试，负值
// 0 成功
// 不成功，但是能继续尝试，正值
#define AVOX_MAP_DECODE_RESULT(XX)     \
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

enum class DecodeResult {
#define XX(name, value, str) name = value,
  AVOX_MAP_DECODE_RESULT(XX)
#undef XX
};

#define AVOX_MAP_AUDIO_RENDER_TYPE(XX) \
  XX(none, 0, "none")                 \
  XX(wasapi, 1, "wasapi")             \
  XX(androidAT, 2, "androidAT")       \
  XX(iosAU, 3, "iosAU")

enum class ARenderType {
#define XX(name, value, str) name = value,
  AVOX_MAP_AUDIO_RENDER_TYPE(XX)
#undef XX
};

// 配置帧添加返回结果
#define AVOX_MAP_CONFIG_ADD(XX)  \
  XX(none, 0, "none")           \
  XX(add, 1, "add")             \
  XX(duplicate, 2, "duplicate") \
  XX(update, 3, "update")       \
  XX(updateSize, 4, "updateSize")

enum class ConfigAddType {
#define XX(name, value, str) name = value,
  AVOX_MAP_CONFIG_ADD(XX)
#undef XX
};

#define AVOX_MAP_SPEED_TYPE(XX) \
  XX(none, 0, "none")          \
  XX(pts, 1, "pts")            \
  XX(server, 2, "server")

enum class SpeedType {
#define XX(name, value, str) name = value,
  AVOX_MAP_SPEED_TYPE(XX)
#undef XX
};

enum class MPOptionType {
  none = 0,
  media_player = 1 << 0,
  io = 1 << 1,
  audio_track = 1 << 2,
  video_track = 1 << 3,
  audio_decoder = 1 << 4,
  video_decoder = 1 << 5,
  audio_render = 1 << 6,
  video_render = 1 << 7,
};

class IPingbackOb {
 public:
  IPingbackOb() = default;
  virtual ~IPingbackOb() = default;

 public:
  virtual void onPingback(int32_t type, const char* data) {};
};

class IMediaPlayerOb {
 public:
  IMediaPlayerOb() = default;
  virtual ~IMediaPlayerOb() = default;

 public:
  // 播放器状态变化
  virtual void onStateChange(PlayerState preState, PlayerState state) {};
  // IO错误返回
  virtual void onIoError(AVError error, const char* msg) {}
  // 解码错误返回
  virtual void onDecodeError(TrackType trackType, DecodeResult error) {}

  // Track信息准备好了
  virtual void onReady() {}
  // 完成
  virtual void onComplete() {}
  virtual void onSeek() {}
  virtual void onPause() {}
  virtual void onResume() {}
  virtual void onClose() {}
};

// 播放器
// swig用napi导出,其默认参数会导致得不到正确结果
class IMediaPlayer {
 public:
  IMediaPlayer() = default;
  virtual ~IMediaPlayer() = default;

 public:
  // 返回内置的参数设置器
  virtual IOption* getOption() = 0;
  // 播放器埋点信息返回
  virtual void setPingbackOb(IPingbackOb* ob) = 0;
  // 这个选项确定使用的IO方案(ffmpeg/zlmediakit),下次打开启用
  virtual void setIoPlan(IoPlan plan) = 0;
  // 这个选项确定视频是否硬解
  virtual void setHardDecode(bool hard) = 0;
  // 设置窗口 window handle/android ANativeWindow/ios CAMetalLayer
  virtual ISurfaceRender* getSurfaceRender() = 0;
  virtual IAudioRender* getAudioRender() = 0;
  // 如果录制的需要转码,叠加水印等,使用bTranscode = true
  virtual IMediaMuxer* getMuxer(bool bTranscode) = 0;
  virtual ISubtitle* getSubtitle() = 0;
  virtual void open(const char* url) = 0;
  virtual void close() = 0;
  virtual void seek(int64_t pos) = 0;
  virtual void pause() = 0;
  virtual void resume() = 0;
  virtual void speed(double speed) = 0;

  virtual PlayerState getState() = 0;
  virtual double getProcess() = 0;
  // 获取媒体源总时间,小于等于0表示直播源不确定结束时间的
  virtual int64_t getDuration() = 0;
  // 当前播放位置,毫秒对应正在渲染的PTS
  virtual int64_t getPosition() = 0;
  // 获取开始时间,注意如果有跳变的PTS,可能从跳变的PTS开始
  virtual int64_t getStartTime() = 0;

  // 需要状态在ready之后才能调用(建议IMediaPlayerOb里的onReady回调里调用)
  virtual ISourceInfo* getSourceInfo() = 0;
  // 获取码率Kb/s,bAvg表示平均还是实时
  virtual double getRate(TrackType type, bool bAvg) = 0;
  // 获取丢包率 (仅RTSP/RTP等UDP协议有效, 返回0.0~1.0)
  virtual float getLossRate(TrackType type) = 0;
  // 获取实时帧率
  virtual double getFps() = 0;
};

// 针对外挂IRawSource的简单播放器,没有音视频相关队列与同步
// 针对AVSource是直出的PCM/YUV/GPUTexture数据，直出播放
class ISourcePlayer {
 public:
  ISourcePlayer() = default;
  virtual ~ISourcePlayer() = default;

 public:
  virtual ISurfaceRender* getSurfaceRender() = 0;
  virtual IAudioRender* getAudioRender() = 0;
  // 只能开流式识别,翻译无效
  virtual ISubtitle* getSubtitle() = 0;
  // 设置设备源里的音频设备
  virtual void setAudioSource(IAudioSource* source) = 0;
  // 设置设备源里的视频设备
  virtual void setVideoSource(IVideoSource* source) = 0;
  // 返回媒体封装器，用于把媒体流封装到文件或网络流
  // 需要在SourcePlayer open之后调用open有效
  virtual IMediaMuxer* getMuxer() = 0;
  virtual bool open() = 0;
  virtual void close() = 0;
  virtual PlayerState getState() = 0;
  // 需要状态在ready之后才能调用
  // 建议IMediaPlayerOb里的onReady回调里调用
  virtual ISourceInfo* getSourceInfo() = 0;
};

// ============== WebRTC 推拉流 ==============

// WebRTC连接状态(映射PeerConnectionState)
#define AVOX_MAP_RTC_CONN_STATE(XX)  \
  XX(init, 0, "new")                \
  XX(connecting, 1, "connecting")   \
  XX(connected, 2, "connected")     \
  XX(disconnected, 3, "disconnected") \
  XX(failed, 4, "failed")           \
  XX(closed, 5, "closed")

enum class RtcConnState {
#define XX(name, value, str) name = value,
  AVOX_MAP_RTC_CONN_STATE(XX)
#undef XX
};

// 轨道方向, inactive=不协商该媒体, 默认sendRecv
enum class RtpDirection {
  inactive = 0,
  recvOnly = 1,
  sendOnly = 2,
  sendRecv = 3,
};

// RTC专属事件回调(独立接口, 与IMediaPlayerOb并列不继承; 经IRtcPlayer::addOb注册,
// addRtcPlayerOb对同时实现二者的对象自动双注册)
// 实现方持有IRtcPlayer指针用于信令回填(自己创建的player, 构造时传入)
// 信令逻辑: onLocalSdp送服务器 → 服务器返回answer → 调player->setRemoteSdp
//           (onIceCandidate同; 也可挂createZlTestSdpAgent走内置ZLM/WHEP信令免写交换)
// onConnectionState/onLocalSdp等来自播放器内部线程, 已做必要保护, 回调内勿重入player
class IRtcEventOb {
 public:
  virtual ~IRtcEventOb() = default;

 public:
  // WebRTC连接状态(Connected=ICE+DTLS完成, 真正连上)
  virtual void onConnectionState(RtcConnState state) {};
  // 收到远端第一帧视频(用于隐藏loading)
  virtual void onFirstVideoFrame() {};
  // 二进制DataChannel消息(需setEnableDataChannel(true))
  virtual void onDataChannelMsg(const char* data, int32_t size) {};
  // 本地SDP生成(信令线程回调; 自定义信令模式下上层自行送出并回填)
  virtual void onLocalSdp(const char* localSdp) {};
  // 本地ICE候选(trickle; candidate为null表示收集完毕)
  virtual void onIceCandidate(const char* candidate, const char* mid,
                              int mlineIndex) {};
};

// 推拉流都可以作为offer/answer
// offer是主动发起方,http请求服务器
// answer是被动接受方,一般长连接建立信令通道
enum class RtcRollType {
  offer,
  answer,
};

// 专门用于处理webrtc对应的推拉流
// 从RtcExport.h移植到这,因为swig/多语言绑定需要此接口
// 而avox_webrtc插件不一定加载
class IRtcPlayer {
 public:
  IRtcPlayer() = default;
  virtual ~IRtcPlayer() = default;

 public:
  // ============ 配置(需在open()前调用, 非线程安全) ============
  // 有长连接信令通道 → 客户端适合作为 Answer 方（被动等待）
  // 没有长连接信令通道 → 客户端适合作为 Offer 方（主动请求）
  // Answer/Offer,其SetLocalDescription/SetRemoteDescription顺序不同
  virtual void setRollType(RtcRollType type) = 0;
  // STUN/TURN服务器,可多次调用; 不配置仅默认STUN(局域网/同网段可直连)
  virtual void addIceServer(const char* uri, const char* username,
                            const char* password) = 0;
  // 轨道方向,默认sendRecv; 只拉流用recvOnly,只推流用sendOnly
  virtual void setVideoDirection(RtpDirection direction) = 0;
  virtual void setAudioDirection(RtpDirection direction) = 0;
  // 推流码率上限Kbps(0=GCC自适应)与帧率上限(0=跟随源)
  virtual void setSendVideoBitrate(int32_t maxKbps) = 0;
  virtual void setSendVideoFps(int32_t maxFps) = 0;
  // 编码偏好:"H264"/"H265"/"AV1"/"VP8"/"VP9", 置顶该codec, 空=默认顺序
  virtual void setPreferredVideoCodec(const char* codec) = 0;
  // 是否创建DataChannel(默认否, 二进制消息)
  virtual void setEnableDataChannel(bool bEnable) = 0;
  // 如果要推视频流,设置本地视频源,否则不设置(未设置则该媒体只收)
  virtual void setVideoSource(IVideoSource* videoSource) = 0;
  // 如果要推音频流,设置本地音频源,否则不设置
  virtual void setAudioSource(IAudioSource* audioSource) = 0;
  // 断线自动重连(仅failed触发, 成功后计数清零; 重连后重新走信令)
  virtual void setAutoReconnect(bool bEnable, int32_t maxRetries) = 0;

 public:
  // ============ 连接 ============
  // 返回PeerConnection创建结果, 信令/连接本身是异步的, 结果看onConnectionState
  virtual bool open() = 0;
  virtual void close() = 0;
  // 手动重连: 重建PeerConnection并重新走信令(offer重发onLocalSdp, answer等远端offer)
  virtual void reconnect() = 0;

 public:
  // ============ SDP/ICE(未用setSignalChannel时手工驱动) ============
  // 得到本地SDP(返回内部缓冲, close后失效, 信令线程会覆盖)
  virtual const char* getLocalSdp() = 0;
  // 设置远端SDP
  virtual void setRemoteSdp(const char* sdp) = 0;
  // 设置ICE候选
  virtual void addIceCandidate(const char* candidate, const char* mid,
                               int mlineIndex) = 0;
  // 供信令agent上报错误(TestSdpOb等内部使用, 上层一般不调):
  // 经onIoError通知观察者, 如 ZLM 返回 -400 stream not found
  virtual void reportSdpError(int64_t code, const char* msg) = 0;

 public:
  // ============ 查询(任意线程) ============
  virtual PlayerState getState() = 0;
  virtual RtcConnState getConnectionState() = 0;
  // 远端视频渲染帧率
  virtual double getFps() = 0;
  // 远端丢包率0.0~1.0(RTCP统计, 未连上返回0)
  virtual float getLossRate() = 0;
  // 往返延迟毫秒(RTCP统计, 未连上返回-1)
  virtual int32_t getRttMs() = 0;
  // 拉流的源信息(onReady后有效)
  virtual ISourceInfo* getRemoteSourceInfo() = 0;
  // 推流的源信息(本地源出描述后有效)
  virtual ISourceInfo* getLocalSourceInfo() = 0;

 public:
  // ============ 渲染 ============
  // 如果设置了videoSource,则返回本地渲染器,否则返回nullptr
  virtual ISurfaceRender* getLocalSurfaceRender() = 0;
  virtual IAudioRender* getLocalAudioRender() = 0;
  // 返回拉流的远端渲染器
  virtual ISurfaceRender* getRemoteSurfaceRender() = 0;
  // 返回拉流的音频渲染(默认接平台设备输出, openTap可取PCM给引擎音频系统)
  virtual IAudioRender* getRemoteAudioRender() = 0;

 public:
  // ============ DataChannel ============
  // 发送二进制消息(需setEnableDataChannel(true)且连接建立)
  virtual bool sendDataChannel(const char* data, int32_t size) = 0;

 public:
  // ============ 观察者 ============
  // rtc扩展回调(onConnectionState/onFirstVideoFrame/onDataChannelMsg)
  // addRtcPlayerOb对IRtcEventOb实例会自动走到这
  virtual void addOb(IRtcEventOb* ob) = 0;
  virtual void removeOb(IRtcEventOb* ob) = 0;
};

extern "C" {
AVOX_EXPORT IMediaPlayer* createMediaPlayer();
AVOX_EXPORT void addMediaPlayerOb(IMediaPlayer* player, IMediaPlayerOb* ob);
AVOX_EXPORT void removeMediaPlayerOb(IMediaPlayer* player, IMediaPlayerOb* ob);
// 创建一个播放设备源的播放器
AVOX_EXPORT ISourcePlayer* createDevicePlayer();
AVOX_EXPORT void addSourcePlayerOb(ISourcePlayer* player, IMediaPlayerOb* ob);
AVOX_EXPORT void removeSourcePlayerOb(ISourcePlayer* player, IMediaPlayerOb* ob);
AVOX_EXPORT const char* getIoPlanStr(IoPlan plan);
AVOX_EXPORT const char* getPlayerStateStr(PlayerState state);
AVOX_EXPORT const char* getTrackTypeStr(TrackType type);
AVOX_EXPORT const char* getDecodeResultStr(DecodeResult error);
AVOX_EXPORT const char* getARenderTypeStr(ARenderType type);
AVOX_EXPORT const char* getConfigAddTypeStr(ConfigAddType type);
AVOX_EXPORT const char* getSpeedTypeStr(SpeedType type);
// WebRTC: 走AvoxManager工厂, 插件注册实现
// 释放约定: SWIG语言层由%newobject自动delete, C++层(UE/Godot)直接delete(虚析构)
AVOX_EXPORT IRtcPlayer* createWebRtcPlayer();
AVOX_EXPORT void addRtcPlayerOb(IRtcPlayer* player, IMediaPlayerOb* ob);
AVOX_EXPORT void removeRtcPlayerOb(IRtcPlayer* player, IMediaPlayerOb* ob);
// 内置信令观察者(ZLM/WHEP HTTP自动交换): addOb挂上后onLocalSdp自动POST远端自动回填
AVOX_EXPORT IRtcEventOb* createZlTestSdpAgent(IRtcPlayer* player,
                                             const char* serverUrl);
AVOX_EXPORT const char* getRtcConnStateStr(RtcConnState state);
}
}
