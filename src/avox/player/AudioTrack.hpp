#pragma once

#include "../AvoxCodec.h"
#include "../audio/ADecoderTask.hpp"
#include "../audio/ARenderTask.hpp"
#include "../audio/AudioDecoder.hpp"
#include "../audio/AudioFrame.hpp"
#include "../audio/AudioRender.hpp"
#include "AVTrack.hpp"

namespace avox {

using AudioTrackPtr = std::shared_ptr<class AudioTrack>;

// decode开启独立的解码线程
// 渲染开户独立的渲染线程
class AudioTrack : public TAVTrack<AudioFramePtr>, public IAudioDecoderOb {
 public:
  AudioTrack();
  virtual ~AudioTrack() = default;

 protected:
  // 解码器
  std::unique_ptr<ADecoderTask> decodeTask = nullptr;
  // 音频渲染
  std::unique_ptr<ARenderTask> renderTask = nullptr;
  // 输入音频描述
  AudioDesc srcDesc = {};
  // 解码后的音频描述
  AudioDesc decodeDesc = {};
  // 解码参数
  ACodecId codecId = ACodecId::none;
  // 音频解码多少毫秒，默认40MS
  int32_t frameMs = 40;
  // bufferMs需要多少buffer
  int32_t frameSize = 0;
  // 当前帧
  AudioFrame curFrame = {};
  // 样本数修正
  int64_t diffAvgCount = 0;
  // 时间差
  double diffCum = 0;
  // 加权平均系数
  double diffAvgCoef = 0.8;
  // 检查音频数据量与PTS间隔时长
  int32_t checkInterval = 2000;
  int32_t checkDataSize = 0;
  // 开始检查,到checkDataInterval间隔判断
  bool startCheck = false;
  int64_t checkPts = AVOX_NOVALID_PTS;
  // 非正常数据,默认false是正常数据
  bool bCheckfail = false;

 public:
  const AudioDesc& getInDesc() { return srcDesc; }
  const AudioDesc& getDecodeDesc() { return decodeDesc; }
  int32_t getFrameMS() { return frameMs; }
  int32_t getFrameSize() { return frameSize; }
  ACodecId getCodecId() { return codecId; }
  // 检查音频长度与PTS间隔是否匹配
  bool matchPtsData() const { return !bCheckfail; }

 public:
  // 解码器输出的音频可能和desc不一样
  void setTrackDesc(const ATrackDesc& trackDesc);
  IAudioRender* getAudioRender();
  // 开始解码,等解码线程线程正常初始化后开始运行渲染线程
  void start();
  // 当与主时钟相差过大时，需要调整输出数据量(采样率)
  int64_t syncAudio();
  void pauseRender(bool pause);
  void pauseDecoder(bool pause);
  // 当队列数据无效时，需要flush
  void flush();
  void updateSeekTime(int64_t seekTime);
  void muxerFrame(const AudioFrame& frame);
  // 当播放器关闭时，关闭相应线程
  void close();

  // AVTrack
 public:
  virtual void onSpeed() override;

  // IAudioDecoderOb
 public:
  // 这里得到AudioDesc desc才是解码后的
  virtual void onAudioDesc() override;
  // 解码器开始解码前的第一个包
  virtual void onPacket(PacketBufPtr packet) override;
  // decode解码线程里，解完后的数据回调
  virtual void onDecode(const AvoxAFrame& frame) override;
  virtual void onAudioComplete() override;
};
}