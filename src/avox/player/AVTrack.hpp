#pragma once

#include <memory>

#include "../module/AvBuffer.hpp"
#include "../module/Ringbuffer.hpp"
#include "../module/TickCounter.hpp"
#include "AVDecoder.hpp"
#include "Clock.hpp"
#include "MPPingback.hpp"
#include "Player.hpp"

namespace avox {

// FFmpeg 原生软解器 (LGPL, 两渠道通用); 历史误写 libx264/libx265 —— 那是编码器名, 不存在同名解码器
#ifndef AVOX_FF_H264_DECODER
#define AVOX_FF_H264_DECODER "h264"
#endif
#ifndef AVOX_FF_H265_DECODER
#define AVOX_FF_H265_DECODER "hevc"
#endif
#define AVOX_ANDROID_H264_DECODER "android h264 decoder"
#define AVOX_ANDROID_H265_DECODER "android h265 decoder"
#define AVOX_IOS_H264_DECODER "ios h264 decoder"
#define AVOX_IOS_H265_DECODER "ios h265 decoder"
#define AVOX_FFVULKAN_H264_DECODER "ff_h264_vulkan"
#define AVOX_FFVULKAN_H265_DECODER "ff_hevc_vulkan"
#define AVOX_FFDX11_H264_DECODER "ff_h264_dx11"
#define AVOX_FFDX11_H265_DECODER "ff_hevc_dx11"
#define AVOX_FFVAAPI_H264_DECODER "ff_h264_vaapi"
#define AVOX_FFVAAPI_H265_DECODER "ff_hevc_vaapi"
// io->packetQueue->decode->frameQueue->render
// 音频/视频Track的共同基类,在处理网络包的逻辑上，这二者差不多是相同的
// 基本配置信息在track里带的AudioDesc/VideoDesc
// 编码信号在带配置信息AvoxPacket包里
// 流程:
// 1. 知道codecid/流信息，其解码器线程就开启,不过解码器这时应该还初始化不了
// 2. 音频需要得到音频配置(常见的通道,采样率等,还需要编码等级等信息),才能初始化
// 3. 视频需要得到SPS/PPS,才能初始化
// 4. 初始化成功/失败，通知外面
class AVTrack : public IPlayerContext, public PtsUpdater {
 public:
  AVTrack();
  virtual ~AVTrack();

 protected:
  // 有对应Track信息后才有效
  bool bVaild = false;
  // 待解码的数据包
  RingBuffer<PacketBufPtr> packetQueue;
  // 自身时钟
  std::unique_ptr<Clock> clock = nullptr;
  // 一路流里可能有多路视频/音频
  int32_t trackId = 0;
  // 后期扩展多路IO流可以在一个播放器内播放
  int32_t streamId = 0;
  // 同步解码器创建与使用
  std::mutex decodeMtx;
  int32_t packCount = 0;
  TrackType trackType = TrackType::none;
  // 解码结束信号
  bool bDecodeEnd = false;

  // 码率统计
  RateCounter rateCounter = {};
  // 队列状态
  PBQueueStatus queueStatus = {};
  // 当前PTS基准是否需要改变，比如flush,close后需要改变
  bool bResetBase = false;
  // 渲染时间，以PTS对应的系统时间为准
  // 控制PTS与系统计时一致
  int64_t renderTime = 0;

  // 记录主时间
  int64_t mClock = 0;

 public:
  bool vaild() { return bVaild; }
  RingBuffer<PacketBufPtr>& getPacketQueue() { return packetQueue; }
  Clock* getClock() { return clock.get(); }

  const RateCounter& getRateCounter() { return rateCounter; }
  const PBQueueStatus& getQueueStatus() { return queueStatus; }

  TrackType getTrackType() { return trackType; }

  void onDecodeError(DecodeResult error);

  MPPingQueue* getMPPingback() { return mpPingback; }
  double getSpeed();
  // 码率Kb/s,是实时还是平均
  double getRate(bool bAvg = false);

 public:
  void setTrackId(int32_t id) { trackId = id; }
  int32_t getTrackId() { return trackId; }

 public:
  // 子类设置流信息后，会调用这个函数，表明当前Track有效了
  void onInitDesc();
  // 子类关闭后，会调用这个函数，表明当前Track无效了
  void onUninitDesc();
  // 子类对变速的处理
  virtual void onSpeed() {};
  // IO线程生产的数据包，交给解码线程
  void pushPacket(const AvoxPacket& data);
  // 解码器线程从packetQueue消费数据
  void pullPacket(PacketBufPtr packet);

 protected:
  // 当IO压入队列前，需要做一些处理
  // 当IO队列与帧队列都满了，清空帧队列数据，保持IO线程能继续
  // 此时应该是消费端(渲染端)出问题了，直接清空
  virtual void onPrePushPacket() = 0;

 public:
  void renderFirst();
  void updateClock(int64_t pts);
  // 设置播放速度，需要记录当前pts
  void setSpeed(double speed);
};

template <typename T>
class TAVTrack : public AVTrack {
 public:
  TAVTrack() = default;
  virtual ~TAVTrack() = default;

 protected:
  // 待渲染的帧
  RingBuffer<T> frameQueue;
  // 统计帧率
  FpsCounter fpsCounter = {};

  // 检查IO是否阻塞太久
  // TickChecker ioBlockChecker = {1000};
  int32_t preQueueIndex = -1;
  int32_t preFrameIndex = -1;

 public:
  RingBuffer<T>& getFrameQueue() { return frameQueue; }
  // 检查帧队列数据存入多少时间
  int64_t getFrameQueueTime();
  // 检查队列缓存了多久,返回时间 = 包尾-帧头
  // 如果帧里没有，返回包队列的时间
  // 如果包没有，返回帧队列的时间
  int64_t getQueueTime();
  // 记录解码器线程生产的帧数据
  void logDecode(int64_t curPts, int32_t bKeyFrame = 0);
  // 渲染线程消费帧数据
  void onFrameResult(bool bGet);
  // 记录RingBuffer的状态
  void recordRingBuffer();

 protected:
  virtual void onPrePushPacket() override;
};

// 返回视频默认的解码器名称
AVOX_EXPORT const char* getDefaultDecoderName(VCodecId codecId, bool bHard);

}
