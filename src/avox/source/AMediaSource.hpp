#pragma once

#include <functional>
#include <vector>

#include "../AvoxMuxer.h"
#include "../audio/AudioDecoder.hpp"
#include "../codec/AudioDecoderChain.hpp"
#include "../codec/VideoDecoderChain.hpp"
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
class AMediaSource : public RawSource,
                     public IAVSourceOb,
                     public IVideoDecoderOb,
                     public IAudioDecoderOb,
                     private VideoDecoderChain::Host,
                     private AudioDecoderChain::Host {
 public:
  AMediaSource();
  virtual ~AMediaSource();

 private:
  std::string uri;
  bool bHardDecode = false;
  // 视频解码器名覆盖(mp.video.decoder.name, onOptionChange缓存), 硬解选型时消费
  std::string videoDecoderName;
  std::unique_ptr<AVSource> ioSource;
  // IO方案,open时创建对应ioSource(默认ffmpeg)
  IoPlan ioPlan = IoPlan::ffmpeg;
  // 候选车道链: 选型与首帧前换道(与播放腿 VDecoderTask 共用同一份策略)
  VideoDecoderChain videoChain;
  // 视频源描述(选型时记下, 换道重建要给新车道重设上下文)
  VideoDesc srcDesc = {};
  // 音频侧同构: 候选链 + 源描述 + 配置包缓存(音频解码器没有 copyConfigs)
  AudioDecoderChain audioChain;
  AudioDesc audioSrcDesc = {};
  std::vector<PacketBufPtr> aConfigPackets;

  bool bOpenVDecoder = false;
  bool bOpenADecoder = false;
  // 视频配置包缓存(首帧前换道时重喂新车道, 只收 vconfig)
  std::vector<PacketBufPtr> vConfigPackets;
  // 换道落点: 半 GOP 起解产缺参考坏帧(播放腿实测), 吸到下一个关键帧再喂
  bool bVDropUntilIdr = false;
  // IO结束标记: IO回调线程写, 消费线程轮询读
  std::atomic<bool> bIoEnd{false};
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
  // 轨级直拷(TransMode): copy轨不建解码器, 原始包经 onRawPacket 转出
  TransMode transMode = TransMode::TranscodeAll;
  bool bVideoCopy = false;
  bool bAudioCopy = false;
  // 音频直拷前置判定(录制器注入, open 前设置): 返回 false = 目标容器容不下该
  // 编码(如 rmvb 的 cook 进 mp4/mov) → 本轨按转码走(建解码器), 不直拷。
  // 未注入 = 不设限
  std::function<bool(ACodecId)> audioCopySupported;

 private:
  // 车道链回调(视频/音频各一组, 按 impl/desc 类型重载): 只做接线/解绑/换道后处置
  virtual bool laneInit(VideoDecoder &impl, const VCodecDesc &desc) override;
  virtual void laneClose(VideoDecoder &impl) override;
  virtual void laneSaveConfigs(VideoDecoder &oldImpl) override;
  virtual void laneChanged(VideoDecoder &newImpl, bool bFirst) override;
  virtual bool laneInit(AudioDecoder &impl, const ACodecDesc &desc) override;
  virtual void laneClose(AudioDecoder &impl) override;
  virtual void laneSaveConfigs(AudioDecoder &oldImpl) override;
  virtual void laneChanged(AudioDecoder &newImpl, bool bFirst) override;
  VideoDecoder *videoDecoder() { return videoChain.impl(); }
  AudioDecoder *audioDecoder() { return audioChain.impl(); }

  // IRawSource
 public:
  virtual bool open() override;
  virtual void close() override;
// 非手动调用close关闭,IO内部处理完毕
// 但是解码还没处理完,需要上层处理
  bool ioComplete();

 public:
  void setUri(const char* url);
  void setHardDecode(bool bHard);
  // 轨道处理方式(open前设置; none丢轨仍由 disableVideo/disableAudio 控制)
  void setTransMode(TransMode mode);
  // 音频直拷前置判定注入(open前设置; 语义见成员注释)
  void setAudioCopySupported(std::function<bool(ACodecId)> supported);
  // 音频实际是否直拷(onReady 后有效): 容器容不下该编码时源层已自行回退转码,
  // 录制器据此决定把源音频包原样进封装还是接解码后的 PCM
  bool isAudioCopy() const { return bAudioCopy; }
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
  bool initVideoDecoder(VCodecId codecId, const VideoDesc& srcDesc_, bool bHard);
  bool initAudioDecoder(ACodecId codecId, const AudioDesc& srcDesc_);
  // 某 track 声明了却一直没来首包,超时后降级该 track
  void resolveTrackWaitTimeout();
};

}
