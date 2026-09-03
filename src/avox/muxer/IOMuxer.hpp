#pragma once
#include <memory>

#include "../module/Ringbuffer.hpp"
#include "../module/RunTask.hpp"
#include "../source/AVSource.hpp"
#include "../source/PacketBuf.hpp"
#include "AudioStream.hpp"
#include "Muxer.hpp"
#include "VideoStream.hpp"

namespace avox {

// 媒体流复用器
// 音频与视频分别把编码后的数据放入队列
// IOMuxer从队列取出数据，进行封装给mp4/rtsp等
class IOMuxer : public IMuxerContext, public RunTask {
 public:
  IOMuxer();
  virtual ~IOMuxer();

 protected:
  VTrackDesc vDesc = {};
  ATrackDesc aDesc = {};
  // 解码后的数据包队列
  RingBuffer<PacketBufPtr> packetQueue;
  // 视频配置帧
  std::vector<PacketBuf> videoConfigs;
  // 音频配置帧
  PacketBufPtr audioConfig = nullptr;
  // 是否有视频
  bool bHaveVideo = false;
  // 是否有音频
  bool bHaveAudio = false;
  // 确定具体实现如ffmepg/zlmediakt把流信息成功写入
  bool bInitStreams = false;
  // 初始化失败,并没有重试的必要
  bool bInitFailed = false;
  std::string url = "";
  // 开始推流的基本包时间
  int64_t basePts = AVOX_NOVALID_PTS;
  // 跨包同帧合并: 多slice编码时, 同dts的多个视频包用append合并
  // 大部分时候为nullptr(不分片), 只有同dts多包时才分配
  PacketBufPtr preBuffer;
  // 分拆包
  std::vector<AvoxPacket> spiltBufs;

 public:
  RingBuffer<PacketBufPtr>& getPacketQueue() { return packetQueue; }

 protected:
  virtual void onRunTask() override;

 public:
  void open(const char* url);
  void setVideoDesc(const VTrackDesc& desc);
  VTrackDesc getVideoDesc() const { return vDesc; }
  void setAudioDesc(const ATrackDesc& desc);
  ATrackDesc getAudioDesc() const { return aDesc; }
  void pushPacket(PacketBufPtr packet);
  void onError(AVError err, const char* msg);
  void close();
  void flushPendingAu();

 protected:
  virtual void onOpen() {};
  virtual bool onInit() = 0;
  virtual void onPushPacket(const AvoxPacket& packet) = 0;
  virtual void onClose() = 0;
};

}