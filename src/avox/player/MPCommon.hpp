#pragma once
#include <memory>
#include <string>

#include "../AvoxCodec.h"
#include "../AvoxLayer.h"
#include "../AvoxLog.h"
#include "../AvoxPlayer.h"
#include "../AvoxTime.h"
#include "../module/Json.hpp"
#include "../video/Window.hpp"
#include "Player.hpp"

namespace avox {

// QoS埋点日志
struct LogMpQoS {
  std::string version;
  std::string traceId;
  std::string traceSeq;
  // request, response
  std::string direct;
  std::string cmdType;
  std::string dataJson;
  Timespan timestamp = {};
};

// 后面的0/1表示频率不高(事件)/频率高(类似每帧变化)
#define AVOX_MAP_MP_PINGBACK(XX)                                                \
  XX(State, 0, "state change", PBStateChange, 0)                               \
  XX(IOStatus, 1, "io status", PBIOStatus, 0)                                  \
  XX(VideoInfo, 2, "video info", PBVideoInfo, 0)                               \
  XX(AudioInfo, 3, "audio info", PBAudioInfo, 0)                               \
  XX(QueueSize, 4, "queue size", PBQueueSize, 1)                               \
  XX(PtsTick, 5, "pts play", PBPtsTick, 1)                                     \
  XX(RateRecord, 6, "rate record", PBRateRecord, 1)                            \
  XX(QueueStatus, 7, "queue status", PBQueueStatus, 0)                         \
  XX(MediaAction, 8, "media action", PBMediaAction, 0)                         \
  XX(SpeedChange, 9, "speed change", PBPlaySpeed, 0)                           \
  XX(WindowStatus, 10, "window status", PBWindowStatus, 0)                     \
  XX(MediaIssue, 100, "media error", PBMediaIssue, 0)

enum class MPPBType {
  none = -1,
#define XX(name, value, str, classtype, tick) name = value,
  AVOX_MAP_MP_PINGBACK(XX)
#undef XX
};

#define AVOX_MAP_PB_QUEUE_TYPE(XX)                                              \
  XX(none, 0, "none")                                                          \
  XX(packet, 1, "packet")                                                      \
  XX(frame, 2, "frame")

enum class PBQueueType {
#define XX(name, value, str) name = value,
  AVOX_MAP_PB_QUEUE_TYPE(XX)
#undef XX
};

#define AVOX_MAP_DIRECTION_TYPE(XX)                                             \
  XX(none, 0, "none")                                                          \
  XX(in, 1, "in")                                                              \
  XX(out, 2, "out")

enum class PBDirectionType {
#define XX(name, value, str) name = value,
  AVOX_MAP_DIRECTION_TYPE(XX)
#undef XX
};

#define AVOX_MAP_MEDIA_OBJECT_TYPE(XX)                                          \
  XX(none, 0, "none")                                                          \
  XX(io, 1, "io")                                                              \
  XX(decode, 2, "decode")                                                      \
  XX(render, 3, "render")                                                      \
  XX(track, 4, "track")                                                        \
  XX(mediaplay, 5, "mediaplay")                                                \
  XX(other, 100, "other")

enum class MediaObject {
#define XX(name, value, str) name = value,
  AVOX_MAP_MEDIA_OBJECT_TYPE(XX)
#undef XX
};

#define AVOX_MAP_MEDIA_OBJECT_ACTION_TYPE(XX)                                   \
  XX(none, 0, "none")                                                          \
  XX(create, 1, "create")                                                      \
  XX(open, 2, "open")                                                          \
  XX(flush, 3, "flush")                                                        \
  XX(pause, 4, "pause")                                                        \
  XX(seek, 5, "seek")                                                          \
  XX(resume, 6, "resume")                                                      \
  XX(stop, 7, "stop")                                                          \
  XX(complete, 8, "complete")                                                  \
  XX(buffing, 9, "buffing")                                                    \
  XX(config, 10, "config")                                                     \
  XX(message, 11, "msg")                                                       \
  XX(reset, 12, "reset")                                                       \
  XX(close, 13, "close")

enum class MediaAction {
#define XX(name, value, str) name = value,
  AVOX_MAP_MEDIA_OBJECT_ACTION_TYPE(XX)
#undef XX
};

#define AVOX_MAP_ACTION_RESULT_TYPE(XX)                                         \
  XX(none, 0, "none")                                                          \
  XX(success, 1, "success")                                                    \
  XX(fail, -1, "fail")

enum class ActionResult {
#define XX(name, value, str) name = value,
  AVOX_MAP_ACTION_RESULT_TYPE(XX)
#undef XX
};

#define AVOX_MAP_WINDOWS_ACTION_TYPE(XX)                                        \
  XX(none, 0, "none")                                                          \
  XX(set, 1, "set")                                                            \
  XX(init, 2, "init")                                                          \
  XX(run, 3, "run")                                                            \
  XX(attach, 4, "attach")                                                      \
  XX(sizechange, 5, "sizechange")                                              \
  XX(detach, 6, "detach")                                                      \
  XX(stop, 7, "stop")                                                          \
  XX(close, 8, "close")

enum class WindowAction {
#define XX(name, value, str) name = value,
  AVOX_MAP_WINDOWS_ACTION_TYPE(XX)
#undef XX
};

class PBCommon {
public:
  virtual ~PBCommon() = default;

public:
  virtual void toJson(Json &jdata) = 0;
  virtual bool fromJson(Json &jdata) { return false; };
  virtual void logPB(std::ostream &o) {}
  virtual LogLevel getLevel() { return LogLevel::info; }
};

struct PBMediaAction : public PBCommon {
public:
  virtual ~PBMediaAction() = default;

public:
  // 操作的对象
  MediaObject mediaObject = MediaObject::none;
  // 操作的类型，decode, render可能是音频或视频
  TrackType trackType = TrackType::none;
  // 操作的动作
  MediaAction action = MediaAction::none;
  // 操作的结果
  ActionResult result = ActionResult::none;
  // 操作的消息
  std::string msg = "";

public:
  virtual void toJson(Json &jdata) final;
  virtual bool fromJson(Json &jdata) final;
  virtual void logPB(std::ostream &o) final;
  virtual LogLevel getLevel() final;
};

// IO返回的状态信息
struct PBIOStatus : public PBCommon {
public:
  virtual ~PBIOStatus() = default;

public:
  // 记录URI
  std::string url = "";
  // IO基准时间
  int64_t baseTimeMs = 0;
  int64_t baseVideoMs = 0;
  int64_t baseAudioMs = 0;
  int32_t videoCount = 0;
  int32_t audioCount = 0;
  int32_t subtitleCount = 0;

public:
  virtual void toJson(Json &jdata) final;
  virtual bool fromJson(Json &jdata) final;
  virtual void logPB(std::ostream &o) final;
};

// 视频信息
struct PBVideoInfo : public PBCommon {
public:
  virtual ~PBVideoInfo() = default;

public:
  // 选择的编码器
  std::string slectCodec = "";
  // 流里编码类型
  VCodecId codecId = VCodecId::none;
  YuvType yuvType = YuvType::other;
  RenderType renderType = RenderType::other;
  int32_t width = 0;
  int32_t height = 0;
  double fps = 0;

public:
  virtual void toJson(Json &jdata) final;
  virtual bool fromJson(Json &jdata) final;
  virtual void logPB(std::ostream &o) final;
};

// 音频信息
struct PBAudioInfo : public PBCommon {
public:
  virtual ~PBAudioInfo() = default;

public:
  std::string slectCodec = "";
  ACodecId codecId = ACodecId::none;
  ARenderType renderType = ARenderType::none;
  // 初始音频参数
  int32_t xsampleRate = 0;
  int32_t xchannels = 0;
  AudioFormat xformat = AudioFormat::other;
  // 解码器可能改变音频参数
  int32_t sampleRate = 0;
  int32_t channels = 0;
  AudioFormat format = AudioFormat::other;

public:
  virtual void toJson(Json &jdata) final;
  virtual bool fromJson(Json &jdata) final;
  virtual void logPB(std::ostream &o) final;
};

// 媒体错误信息
struct PBMediaIssue : public PBCommon {
public:
  virtual ~PBMediaIssue() = default;

public:
  TrackType trackType = TrackType::none;
  MediaObject errorType = MediaObject::none;
  int32_t code = 0;
  std::string msg = "";

public:
  virtual void toJson(Json &jdata) final;
  virtual bool fromJson(Json &jdata) final;
  virtual void logPB(std::ostream &o) final;
  virtual LogLevel getLevel() final;
};

// 记录状态变化
struct PBStateChange : public PBCommon {
public:
  virtual ~PBStateChange() = default;

public:
  PlayerState preState = PlayerState::none;
  PlayerState state = PlayerState::none;

public:
  virtual void toJson(Json &jdata) final;
  virtual bool fromJson(Json &jdata) final;
  virtual void logPB(std::ostream &o) final;
};

// track里pack/frame队列进出变化
// IO线程产生packet包
// 解码线程消费packet产生frame
// 渲染线程消费frame
struct PBQueueSize : public PBCommon {
public:
  virtual ~PBQueueSize() = default;

public:
  // Track类型
  TrackType trackType = TrackType::none;
  // 队列类型
  PBQueueType queueType = PBQueueType::none;
  // 队列变化方向
  PBDirectionType directionType = PBDirectionType::none;
  // 队列大小
  int32_t queueSize = 0;
  // 数据的PTS
  int64_t dataPts = 0;
  // 只在包时记录，frame时不记录
  int32_t dataSize = 0;
  // 帧类型(暂定0:普通 1:关键帧)
  int32_t frameType = 0;

public:
  virtual void toJson(Json &jdata) final;
  virtual bool fromJson(Json &jdata) final;
  virtual void logPB(std::ostream &o) final;
};

// track里pack/frame RingBuffer的状态
struct PBQueueStatus : public PBCommon {
public:
  virtual ~PBQueueStatus() = default;

public:
  // Track类型
  TrackType trackType = TrackType::none;
  // 渲染时间，帧队列的开始时间戳
  int64_t renderTime = AVOX_NOVALID_PTS;
  // 解码输出时间，帧队列的结束时间戳
  int64_t decodeOutTime = AVOX_NOVALID_PTS;
  // 解码线程从包队列拿的时间，包队列的开始时间戳
  int64_t decodeInTime = AVOX_NOVALID_PTS;
  // IO线程插入最新包时间，包队列的结束时间戳
  int64_t ioTime = AVOX_NOVALID_PTS;

  int32_t queueSize = 0;
  int32_t frameSize = 0;

public:
  virtual void toJson(Json &jdata) final;
  virtual bool fromJson(Json &jdata) final;
  virtual void logPB(std::ostream &o) final;
};

// 播放进度
struct PBPtsTick : public PBCommon {
public:
  virtual ~PBPtsTick() = default;

public:
  TrackType trackType = TrackType::none;
  int64_t pts = 0;

public:
  virtual void toJson(Json &jdata) final;
  virtual bool fromJson(Json &jdata) final;
  virtual void logPB(std::ostream &o) final;
};

// 播放码率
struct PBRateRecord : public PBCommon {
public:
  virtual ~PBRateRecord() = default;

public:
  TrackType trackType = TrackType::none;
  double rate = 0.0;

public:
  virtual void toJson(Json &jdata) final;
  virtual bool fromJson(Json &jdata) final;
  virtual void logPB(std::ostream &o) final;
};

// 播放速度
struct PBPlaySpeed : public PBCommon {
public:
  virtual ~PBPlaySpeed() = default;

public:
  double speed = 0.0;

public:
  virtual void toJson(Json &jdata) final;
  virtual bool fromJson(Json &jdata) final;
  virtual void logPB(std::ostream &o) final;
};

// 窗口操作
struct PBWindowStatus : public PBCommon {
public:
  virtual ~PBWindowStatus() = default;

public:
  WindowAction action = WindowAction::none;
  RenderType renderType = RenderType::other;
  int32_t index = 0;
  int32_t width = 0;
  int32_t height = 0;

public:
  void getWindowInfo(Window *win);

public:
  virtual void toJson(Json &jdata) final;
  virtual bool fromJson(Json &jdata) final;
  virtual void logPB(std::ostream &o) final;
};

const char *getMPPBTypeStr(MPPBType type);
const char *getQueuTypeStr(PBQueueType type);
const char *getDirectionTypeStr(PBDirectionType type);
const char *getMediaObjectStr(MediaObject type);
const char *getMediaActionStr(MediaAction type);
const char *getActionResultStr(ActionResult type);

}