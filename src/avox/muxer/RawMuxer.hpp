#pragma once

#include "../source/RawSource.hpp"
#include "MediaMuxer.hpp"

namespace avox {

// 媒体封装器，在MediaMuxer基础上
// 整合编码器处理原始YUV/PCM数据流
// 不把RawSource与RawMuxer合一起
// 因为上层需要同时控制并得知这二者状态
// 后面扩展VkVideoRender设置大小,FFResample重采样
class RawMuxer : public MediaMuxer {
 public:
  RawMuxer();
  virtual ~RawMuxer();

 protected:
  // 音频帧编码信息，如果传入PCM数据，则需要编码器
  // 如果是AAC数据，音频编码信息直接从外面传入
  std::unique_ptr<AudioStream> audioStream = nullptr;
  ACodecId aCodecId = ACodecId::aac;
  // 视频帧编码信息，如果传入的YUV数据则需要编码器
  // 如果是H264/H265数据，视频编码信息直接从外面传入
  std::unique_ptr<VideoStream> videoStream = nullptr;
  VCodecId vCodecId = VCodecId::h265;
  bool bOpenSource = false;
  // 音频与视频的回调可能在不同线程
  // 二边线程访问同一资源时用的锁
  std::mutex avMutex;
  // 输出地址
  std::string url = "";
  // 音频编码自带重采样,利用这个加上音频重采样
  bool bReAudio = false;
  AudioDesc outADesc = {}; 

 public:
  bool getHardEncode();
  virtual void setHardEncode(bool bHard) override;
  virtual void setVideoCodec(VCodecId codecId) override;
  virtual void setAudioCodec(ACodecId codecId) override;
  virtual void setAudioDesc(const AudioDesc& desc) override;
  virtual void setInVideoDesc(const VTrackDesc& desc) override;
  virtual void setInAudioDesc(const ATrackDesc& desc) override;
  virtual void onOpen() override;
  virtual void onClose() override;  
  void pushFrame(const YUVFrame& frame);
  void pushFrame(const GpuFrame& frame);
  void pushFrame(const AvoxAFrame& frame);
  // seek 时 flush 编码管线:重置编码器 GOP,清内部缓冲
  void flushEncoders() {
    if (videoStream) videoStream->flushEncoder();
    if (audioStream) audioStream->flushEncoder();
  }
};

}
