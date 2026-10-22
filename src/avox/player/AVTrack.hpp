#pragma once

#include <memory>

#include "../codec/CodecChain.hpp"
#include "../module/AvBuffer.hpp"
#include "../module/Ringbuffer.hpp"
#include "../module/TickCounter.hpp"
#include "AVDecoder.hpp"
#include "Clock.hpp"
#include "MPPingback.hpp"
#include "Player.hpp"

namespace avox {

class PacketBufPool;

// io->packetQueue->decode->frameQueue->render
// 音频/视频Track的共同基类,在处理网络包的逻辑上，这二者差不多是相同的
// 基本配置信息在track里带的AudioDesc/VideoDesc
class AVTrack : public IPlayerContext, public PtsUpdater {
 public:
  AVTrack();
  virtual ~AVTrack();

 protected:
  // 有对应Track信息后才有效
  bool bVaild = false;
  // 待解码的数据包
  RingBuffer<PacketBufPtr> packetQueue;
  // 包缓冲池: 按尺寸档复用(小包永不被大包撑大), 稳态下不再 malloc。
  // 构造时创建、终生不替换 —— 切音轨时命令线程的替换与 IO 线程的拷取会构成
  // shared_ptr 非原子并发读写(UB); 复用同一池只做 reset 清囤积, 天然无竞争
  std::shared_ptr<PacketBufPool> packetPool;
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
  // 基准重置后的保护窗截止时刻: 窗内过期帧不丢只快刷,
  // 吸收起播/seek首拍渲染链惰性初始化(百ms级,簇尾可至0.56s)造成的落后, 避免跳帧
  int64_t warmupUntilMs = 0;

  // 记录主时间
  int64_t mClock = 0;

 public:
  bool vaild() { return bVaild; }
  RingBuffer<PacketBufPtr>& getPacketQueue() { return packetQueue; }
  const std::shared_ptr<PacketBufPool>& getPacketPool() { return packetPool; }
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
  virtual void onPrePushPacket() = 0;

 public:
  void renderFirst();
  void updateClock(int64_t pts);
  // 设置播放速度，需要记录当前pts
  void setSpeed(double speed);

 protected:
  // 播放器是否存在有效视频轨(决定纯音频时音频是否喂主时钟)
  bool hasVaildVideoTrack();
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

}
