#pragma once

#include "../source/RawSource.hpp"
#include "../video/VideoEncodeParams.hpp"
#include "MediaMuxer.hpp"

namespace avox {

// 媒体封装器，在MediaMuxer基础上
// 整合编码器处理原始YUV/PCM数据流
// 不把RawSource与RawMuxer合一起
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
  // 声明的输出规格(setVideoDesc): 尺寸由渲染链缩放, fps>0 时按 pts 丢帧
  VideoDesc outVideoDesc = {};
  bool bSetOutSize = false;
  // 帧率门状态(会话/新段复位)
  FpsGateState fpsGate = {};
  // 连续尺寸不符帧数: 单帧瞬态(换图/首帧)不算, 持续不符才判定渲染腿没缩放
  int32_t sizeMismatchFrames = 0;
  bool bWarnedSizeMismatch = false;
  // 帧与声明输出尺寸是否一致(不符则丢, 防流头/编码器描述与实际帧错位)
  bool checkFrameSize(int32_t width, int32_t height);

 public:
  bool getHardEncode();
  virtual void setHardEncode(bool bHard) override;
  // 视频编码参数(码控模式/质量/码率/预设/profile/关键帧间隔), open 前设置生效
  void setEncodeParam(const VideoEncodeParam& param);
  // 输出规格(投屏转码腿用): 尺寸/帧率都走 setVideoDesc(缩放由渲染链做)。
  // open 前设置; 开录后调用被忽略(流头与编码器描述已定)
  virtual void setVideoDesc(const VideoDesc& desc) override;
  virtual void setVideoEncodeParam(const VideoEncodeParam& param) override;
  bool hasOutVideoDesc() const { return bSetOutSize; }
  const VideoDesc& getOutVideoDesc() const { return outVideoDesc; }
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
  // seek 时 flush 编码管线:重置编码器 GOP,清内部缓冲; 帧率门也重设基准
  void flushEncoders() {
    fpsGate.reset();
    if (videoStream) videoStream->flushEncoder();
    if (audioStream) audioStream->flushEncoder();
  }
  // 停两条流的编码器(幂等; close 与析构共用)
  void stopStreams();
};

}
