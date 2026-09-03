#pragma once

#include "../AvoxMuxer.h"
#include "../audio/AudioDecoder.hpp"
#include "../module/JsonOption.hpp"
#include "../module/TickCounter.hpp"
#include "../player/AVDecoder.hpp"
#include "../video/VideoDecoder.hpp"
#include "RawSource.hpp"
#include "AVSource.hpp"

namespace avox {

class AVSource;

// 文件/网络流解码数据源
// 封装 IO + 解码，输出 YUV/GPU/PCM 帧
// 类似 DeviceSource，但数据来自文件/网络流而非设备
// 经RawSource→BaseSource已是OptionLink, linkOption/getLink直接可用
class AMediaSource : public RawSource,
                     public IAVSourceOb,
                     public IVideoDecoderOb,
                     public IAudioDecoderOb {
 public:
  AMediaSource();
  virtual ~AMediaSource();

 private:
  std::string uri;
  bool bHardDecode = false;
  std::unique_ptr<AVSource> ioSource;
  // IO方案,open时创建对应ioSource(默认ffmpeg)
  IoPlan ioPlan = IoPlan::ffmpeg;
  std::unique_ptr<VideoDecoder> videoDecoder;
  std::unique_ptr<AudioDecoder> audioDecoder;
  //
  bool bOpenVDecoder = false;
  bool bOpenADecoder = false;
  bool bIoEnd = false;
  // seek 状态:IO 线程检查,编码线程写
  std::atomic<bool> bFlushPending{false};
  std::atomic<bool> bDiscardPacket{false};
  //
  // 音视频首包到达时间,默认无效
  int64_t videoFirstPts = AVOX_NOVALID_PTS;
  int64_t audioFirstPts = AVOX_NOVALID_PTS;
  // 以先到那路为基准,等待另一路的超时器(成员持久,用 reset 复用)
  TickChecker trackWaitChecker{3000};
  bool bTrackWaitStarted = false;
  bool bTrackWaitResolved = false;

  // IRawSource
 public:
  virtual bool open() override;
  virtual void close() override;
  // 非手动调用close关闭,IO内部处理完毕
  // 但是解码还没处理完,需要上层处理
  // 上层有队列的话,在队列为空则IO完成,则可判定结束
  bool ioComplete();

 public:
  void setUri(const char* url);
  void setHardDecode(bool bHard);
  // 设置IO方案(open前设置,默认ffmpeg)
  void setIoPlan(IoPlan plan);
  // 获取内部的ioSource,在open后可用
  AVSource* getSource() {return ioSource.get();}
  // seek 前置:置 flush+discard flag,IO 线程在 onPacket 中执行 flush 并丢弃后续包
  void preSeek();
  // seek 执行:调 io seekTo 并解除 discard,IO 线程恢复处理包
  void seek(int64_t posMs);

  // IAVSourceOb
 public:
  virtual void onReady() override;
  virtual void onSyncPts() override;
  virtual void onClose() override;
  virtual void onComplete() override;
  virtual void onError(AVError error, const char* msg) override;
  virtual void onPacket(const AvoxPacket& packet) override;

  // IVideoDecoderOb
 public:
  virtual void onVideoDesc() override;
  virtual void onPacket(PacketBufPtr packet) override;
  virtual void onDecode(const YUVFrame& frame) override;
  virtual void onDecodeGpu(const GpuFrame& frame) override;
  virtual void onVideoComplete() override;

  // IAudioDecoderOb
 public:
  virtual void onAudioDesc() override;
  virtual void onDecode(const AvoxAFrame& frame) override;
  virtual void onAudioComplete() override;

  // IOptionOb(OptionLink): 上层选项变化钩子;io.*键由内部ioSource自行观察,此处留扩展点
 public:
  virtual void onOptionChange(const char* key, ArgType option) override;

 private:
  bool initVideoDecoder(VCodecId codecId, const VideoDesc& srcDesc, bool bHard);
  bool initAudioDecoder(ACodecId codecId, const AudioDesc& srcDesc);
  // 某 track 声明了却一直没来首包,超时后降级该 track
  void resolveTrackWaitTimeout();
};

}
