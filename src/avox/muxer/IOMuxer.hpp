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
  // dts 回退体检(2026-09-22): muxer 硬要求 dts 非递减, 但上游可能给出回退值
  // (B帧解码顺序 / seek 重新定位 / PTS 无效被上游合成)。此处**只计数不修正**:
  // 修正会掩盖上游缺陷, 而计数让每次录制自带"时间轴有没有被搞坏"的证据。
  // 音视频 dts 各自单调(见 pushPacket 音频分支注释), 必须分流跟踪 —— 混在一起
  // 会因音视频交错而全是误报。
  int64_t lastMuxVideoDts = AVOX_NOVALID_PTS;
  int64_t lastMuxAudioDts = AVOX_NOVALID_PTS;
  uint64_t dtsBackwardCount = 0;
  // 音频NOPTS合成(录制链): 源层音频垃圾pts归一NOPTS透传后, 录制没有解码采样
  // 轴可推进, NOPTS直进会把basePts毒化成INT64_MIN或写出巨大负dts被封装端拒绝。
  // 以首个有效pts锚定, NOPTS包按 包duration>已学习步进>40ms保底 推进; 有效锚点
  // 靠前则重锚回吸漂移, 靠后只学习步进(保dts单调)
  int64_t audioSynthPts = AVOX_NOVALID_PTS;
  int64_t audioSynthStepMs = 0;  // 0=未学习
  int64_t audioAnchorPts = AVOX_NOVALID_PTS;
  int32_t audioAnchorPackets = 0;
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
  // 记录一个"真正交给 muxer"的包 dts(见成员注释); dts 回退时计数, 首次回退打一条 warn
  void noteMuxDts(int64_t dts, int32_t packtype);
  // dts 回退累计次数(0 = 本次录制时间轴全程非递减)
  uint64_t getDtsBackwardCount() const { return dtsBackwardCount; }
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