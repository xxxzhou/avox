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


// 外挂字幕候选项(a01-T5): listSubtitleCandidates 扫描结果的只读接口。托管对象:
// 引擎缓存最近一次扫描, 生命周期至下次 listSubtitleCandidates/换源/close, 勿释放
class ISubtitleCandidate {
 public:
  virtual ~ISubtitleCandidate() = default;
  // UTF-8 绝对路径
  virtual const char* getPath() const = 0;
  // srt / ass(.ssa 归 ass)
  virtual SCodecId getCodec() const = 0;
  // 语言标记 hint(小写原名 zh/chs/cht/gb/big5/eng..., 空=未标)
  virtual const char* getLang() const = 0;
  // 1=带 .gbk 编码后缀(内容大概率 GBK), 0=无
  virtual int32_t getGbkHint() const = 0;
};

// 观感设置生效矩阵(样式计划 doc/plan/player/字幕样式设计.md):
//   纯文本样式(setFont/setColor/setAlign/setPosition/setPositionMargin/
//   setMaxWidth) → 仅 SRT/ASR 文本路径生效; 内封 ASS 轨/外挂 .ass 样式归
//   片源(libass)、PGS 是位图, 静默忽略。
//   全局变换(setScale/setOffset/setOpacity) → 三层通用: 文本在 CPU 侧重
//   栅格化(放大清晰), ASS/PGS 在 canvas 合成处重采样。
//   ASS 轨覆盖(setAssScale/setAssFont) → 仅内封 ASS 轨/外挂 .ass 的 libass
//   排版层生效(libass 重排, 放大清晰非位图拉伸), 叠加在全局变换之上;
//   SRT/ASR 文本路径与 PGS 忽略。
class ISubtitle {
 public:
  virtual ~ISubtitle() = default;
  // 启用 ASR(激活 asr 槽: 拆被顶掉的内封轨/外挂槽, 启动识别)
  virtual void enableAsr() = 0;
  // 关闭 ASR(仅当 asr 槽是当前胜者时清; 不影响内封轨/外挂槽)
  virtual void disableAsr() = 0;

  // ---- 全局变换: SRT/ASR + ASS + PGS 通用 ----
  // 整层缩放, 1.0=原尺寸(<=0 忽略, 保持上次有效值)
  virtual void setScale(float scale) {}
  // 帧归一化平移(0.1=帧宽/帧高的 10%), 不钳制, 越界由帧内钳制/可见区裁剪兜底
  virtual void setOffset(float offsetX, float offsetY) {}
  // 整层不透明度 0.0~1.0(0=隐藏, 1=不透明, 越界钳制)
  virtual void setOpacity(float opacity) {}

  // ---- 纯文本样式: 仅 SRT/ASR 生效(ASS/PGS 静默忽略) ----
  // 字体名(asset/fonts 下, FontMap 解析; nullptr/空串保持现值)与字号
  // (1080 基准像素, 随帧高缩放; <=0 忽略)
  virtual void setFont(const char* fontName, int32_t fontSize) {}
  // 文字颜色 0.0~1.0(逐分量钳制)
  virtual void setColor(float r, float g, float b) {}
  // 对齐(决定文本块相对锚点的摆放与边距内收方向; none=该轴保持现值)。
  // 默认 mid/bottom
  virtual void setAlign(HAlignType h, VAlignType v) {}
  // 帧归一化锚点(0~1 钳制, 默认 0.5/0.8)
  virtual void setPosition(float anchorX, float anchorY) {}
  // 对齐方向上的内收边距(1080 基准像素, 随帧高缩放; 负值取 0)
  virtual void setPositionMargin(float marginX, float marginY) {}
  // 自动换行宽度(帧宽比例, 默认 0.8; <=0 忽略, >1 钳到 1)
  virtual void setMaxWidth(float ratio) {}

  // 外挂字幕编码探测结果(文本与 .ass/.ssa 插件路径加载后均可查, 换源/卸载复位
  // unknown; UTF-16 文件渲染不可用但编码仍可查)。带默认实现: 既有实现者零影响
  virtual SubtitleEncoding getFileEncoding() { return SubtitleEncoding::unknown; }

  // 字幕整体延迟(a01-T3): 正=延后显示, 负=提前, 0=同步(默认)。作用于外挂
  // 文本/内封 ASS/PGS 三路内容选择; ASR 为实时口播不适用。带默认实现: 既有
  // 实现者零影响
  virtual void setDelay(int64_t delayMs) {}

  // 外挂候选枚举(a01-T5): 扫同目录「同主名」字幕排序后缓存, 返回个数(0=无候选/
  // 远程路径, 负=参数非法), 经 getSubtitleCandidate 取用(默认实现返回 0)
  virtual int32_t listSubtitleCandidates(const char* videoUrl) {
    (void)videoUrl;
    return 0;
  }
  // 取最近一次 listSubtitleCandidates 缓存的第 index 个候选(越界返回 nullptr),
  // 生命周期见 ISubtitleCandidate。带默认实现: 既有实现者零影响
  virtual ISubtitleCandidate* getSubtitleCandidate(int32_t index) {
    (void)index;
    return nullptr;
  }

  // ---- ASS 轨样式覆盖(a01-T3): 生效面见类注释矩阵; 带默认实现既有实现者零影响 ----
  // ASS 内容缩放(libass font_scale 排版重排, 放大清晰非位图拉伸): 1.0=片源
  // 原样(默认), <=0 忽略保持现值; 与全局 setScale 独立(canvas 合成层再乘)
  virtual void setAssScale(float scale) {}
  // ASS 字体覆盖: 替换片源样式的字体名(nullptr/空=不覆盖; 已应用覆盖的轨
  // 不回滚, 重载轨生效)。字体需系统字体提供器或 setFontsDir 可供
  virtual void setAssFont(const char* family) {}
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
  XX(iosAU, 3, "iosAU")               \
  XX(pulse, 4, "pulse")

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
  // HDR静态元数据(ST2086/CTA-861.3)就绪, 每次开流至多一次, 无元数据的流不回调
  // 用途: 宿主侧HDR标识/显示器HDR模式切换/决策 ISurfaceRender::setHdrMode
  virtual void onHdrMeta(const HdrMeta& hdrMeta) {}

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
  // 选择内封字幕轨(ISourceInfo::getSubtitleDesc 的局部索引, -1=关闭)。
  // 走 avox_ass 插件渲染(计划 ASS字幕渲染计划.md); 未装插件时选轨无效(降级)。
  // 带默认实现: 既有 IMediaPlayer 实现者零影响
  virtual void setSubtitleTrack(int32_t index) {}
  // 选择内封音轨(ISourceInfo::getAudioDesc 的局部索引, -1=关闭音频)。
  // 默认播放第 0 条; 其余轨不解码不出声, 随时可切。切轨即时生效(旧轨停,
  // 新轨从当前播放位置续上)。带默认实现: 既有 IMediaPlayer 实现者零影响
  virtual void setAudioTrack(int32_t index) {}
  // 外挂字幕文件唯一入口(.ass/.srt, 内部按扩展名分流渲染路径), 激活外挂槽
  // (清内封轨与 ASR — 三槽位后激活者胜); 无插件且非文本文件返回 false
  virtual bool loadSubtitle(const char* path) {
    (void)path;
    return false;
  }
  // 关闭外挂槽(仅当外挂是当前胜者时清; 不影响内封轨/ASR), 返回是否真的关了
  virtual bool unloadSubtitle() { return false; }

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
  // ============ SDP/ICE(自定义信令时手工驱动, 内置信令走createZlTestSdpAgent) ============
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
