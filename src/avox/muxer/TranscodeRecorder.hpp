#pragma once

#include "../audio/AudioFrame.hpp"
#include "../audio/AudioRender.hpp"
#include "../module/JsonOption.hpp"
#include "../module/Ringbuffer.hpp"
#include "../source/AMediaSource.hpp"
#include "../video/SurfaceRenderVk.hpp"
#include "../video/VideoFrame.hpp"
#include "RawMuxer.hpp"

namespace avox {

// 转码录制器：将网络流/本地文件解码
// 通过VkVideoRender处理（缩放/水印等），重新编码保存
// 解码与编码通过队列解耦，队列满时阻塞解码线程反压到IO层
class TranscodeRecorder : public IRecorder,
                          public JsonOption,
                          public IRawSourceOb,
                          public RunTask,
                          public Observer<IRecorderOb> {
 public:
  TranscodeRecorder();
  virtual ~TranscodeRecorder();

 protected:
  // IO源+解码器
  std::unique_ptr<AMediaSource> source;
  // IO源
  AVSource* ioSource = nullptr;
  // 编码器+IO目的
  std::unique_ptr<RawMuxer> muxer;
  std::unique_ptr<SurfaceRenderVk> surfaceRender;
  // 裸 AudioRender(无设备输出),用于 AudioTap 读取音频数据
  std::unique_ptr<AudioRender> audioRender;
  RecorderState state = RecorderState::none;
  IoPlan ioPlan = IoPlan::ffmpeg;
  MuxerType muxerType = MuxerType::ffmpeg;
  std::string inputUrl;
  std::string outputFile;
  // outputFile 为空时转码直出,不编码写文件,经 ISurfaceRender/IAudioRender 对外
  bool bNoOutput = false;
  bool bHardDecode = false;
  // 默认走平台原生硬编(AndVEncoder/IOSVEncoder/ff_*_dx11); 需要更小文件可显式关走 FFmpeg 软编
  bool bHardEncode = true;
  // 输出描述，未设置或与源相同则不处理
  VideoDesc outVideoDesc = {};
  AudioDesc outAudioDesc = {};
  bool bSetOutVideo = false;
  bool bSetOutAudio = false;
  // 帧队列，上限5，满时阻塞解码线程
  RingBuffer<VideoFramePtr> vFrameQueue{5};
  RingBuffer<AudioFramePtr> aFrameQueue{5};
  // to()取split帧的重排副本(仅420P/422P带padding时实际拷贝),编码线程私有
  std::unique_ptr<ImageBuffer> splitBuffer;
  // 进度
  RecorderProgress progress = {};
  // seek 状态(均跨线程:调用线程写,编码/IO 线程读)
  std::atomic<bool> bSeeking{false};        // seek 进行中(IO 回调丢弃帧)
  std::atomic<bool> bSeekPending{false};    // 编码线程待处理 seek
  std::atomic<int64_t> seekTargetMs{0};     // 绝对 PTS(已 +baseTime)
  //
  ACodecId aCodecid = ACodecId::aac;
  // 默认 H.264: 硬编兼容性远好于 h265(低端安卓/老设备 hevc 编码器常缺失), 兼容性敏感的转码录不赌设备能力
  VCodecId vCodecId = VCodecId::h264;

  // IRecorder
 public:
  virtual void setIoPlan(IoPlan plan) override;
  virtual void setMuxerType(MuxerType type) override;
  virtual void setVideoCodec(VCodecId codecId) override;
  virtual void setAudioCodec(ACodecId codecId) override;
  virtual ISurfaceRender* getSurfaceRender() override ;
  virtual IAudioRender* getAudioRender();
  virtual bool open(const char* inputUrl, const char* outputFile) override;
  virtual void close() override;
  virtual RecorderState getState() override { return state; }
  virtual bool seek(int64_t posMs) override;
  virtual int64_t getDuration() override;
  virtual ISourceInfo* getSourceInfo() override;
  // 返回内置的参数设置器(open前设置生效)
  virtual IOption* getOption() override { return this; }

  // JsonOption
 public:
  // 有改动才同步成员,未设置的key走成员默认值
  virtual void onOptionChange(const char* key, ArgType option) override;

 public:
  void setVideoDesc(const VideoDesc& desc);
  void setAudioDesc(const AudioDesc& desc);

  // IRawSourceOb - 解码回调，enqueueWait入队
 public:
  virtual void onReady() override;
  virtual void onClose() override;
  virtual void onError(AVError error, const char* msg) override;
  virtual void onVideoFrame(const YUVFrame& frame, int32_t trackId) override;
  virtual void onGpuFrame(const GpuFrame& frame, int32_t trackId) override;
  virtual void onAudioFrame(const AvoxAFrame& frame, int32_t trackId) override;

  // RunTask - 编码线程
 protected:
  virtual void onRunTask() override;

 private:
  void setRecState(RecorderState newState);
  void processVideo(VideoFramePtr frame);
  void processAudio(AudioFramePtr frame);
  // 更新并节流派发进度(音视频都在时避免回调翻倍)
  void updateProgress();
  // seek 全流程(编码线程):暂停IO→清队列→flush解码/编码→seekTo→恢复
  void doSeek();
};

}
