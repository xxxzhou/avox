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

// 前置声明(实现在 CpuQEnhancer.hpp, 仅 .cpp 使用; 避免在此拉入改变
// Window.hpp 平台宏解析顺序的 include 链)
class CpuQEnhancer;

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
  // 离线画质增强: 图只做 yuv→rgba, rgba 帧入队, 编码线程侧推理(队列满反压解码)
  bool bQEnhance = false;
  QualityEnhanceParamet qparamet = {};
  std::unique_ptr<CpuQEnhancer> qenhancer;
  std::shared_ptr<ImageBuffer> rgbaBuffer;
  // 帧队列，上限5，满时阻塞解码线程
  RingBuffer<VideoFramePtr> vFrameQueue{5};
  RingBuffer<AudioFramePtr> aFrameQueue{5};
  // to()取split帧的重排副本(仅420P/422P带padding时实际拷贝),编码线程私有
  std::unique_ptr<ImageBuffer> splitBuffer;
  // 进度
  RecorderProgress progress = {};
  // 进度写入锁: copy轨(IO线程 onRawPacket)与帧路径(编码线程)并发驱动
  std::mutex progressMtx;
  // seek 状态(均跨线程:调用线程写,编码/IO 线程读)
  std::atomic<bool> bSeeking{false};        // seek 进行中(IO 回调丢弃帧)
  std::atomic<bool> bSeekPending{false};    // 编码线程待处理 seek
  std::atomic<int64_t> seekTargetMs{0};     // 绝对 PTS(已 +baseTime)
  // close收尾: close线程只表达意图, 排空(EOF后编完残帧)/丢弃由编码线程裁决(source仅它可读)
  std::atomic<bool> bStopPending{false};    // close已请求, 待编码线程收尾
  std::atomic<bool> bDrainPhase{false};     // 收尾排空中(running已false), process放行残帧
  //
  ACodecId aCodecid = ACodecId::aac;
  // 默认 H.264: 硬编兼容性远好于 h265(低端安卓/老设备 hevc 编码器常缺失), 兼容性敏感的转码录不赌设备能力
  VCodecId vCodecId = VCodecId::h264;
  // 轨级直拷(none丢轨优先于copy; 空输出无封装目标自动回退转码)
  TransMode transMode = TransMode::TranscodeAll;
  bool bVideoCopy = false;
  bool bAudioCopy = false;

  // IRecorder
 public:
  virtual void setIoPlan(IoPlan plan) override;
  virtual void setMuxerType(MuxerType type) override;
  virtual void setVideoCodec(VCodecId codecId) override;
  virtual void setAudioCodec(ACodecId codecId) override;
  virtual void setTransMode(TransMode mode) override;
  virtual void setVideoDesc(const VideoDesc& desc) override;
  virtual void setAudioDesc(const AudioDesc& desc) override;
  virtual void enableQualityEnhance(const QualityEnhanceParamet& paramet) override;
  virtual void disableQualityEnhance() override;
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

  // IRawSourceOb - 解码回调，enqueueWait入队
 public:
  virtual void onReady() override;
  virtual void onClose() override;
  virtual void onError(AVError error, const char* msg) override;
  virtual void onVideoFrame(const YUVFrame& frame, int32_t trackId) override;
  virtual void onGpuFrame(const GpuFrame& frame, int32_t trackId) override;
  virtual void onAudioFrame(const AvoxAFrame& frame, int32_t trackId) override;
  virtual void onRawPacket(const AvoxPacket& packet) override;

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
