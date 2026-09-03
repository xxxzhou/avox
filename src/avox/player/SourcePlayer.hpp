#pragma once

#include "../audio/ARenderTask.hpp"
#include "../audio/AudioOutput.hpp"
#include "../muxer/RawMuxer.hpp"
#include "../source/DeviceSource.hpp"
#include "../subtitle/SubtitleView.hpp"
#include "../video/VideoRender.hpp"
#include "../video/WindowRender.hpp"
#include "BasePlayer.hpp"

namespace avox {

// 不同于MediaPlayer,这里没有IO包/帧队列
// 音频与视频对应的是解码后的数据,PCM/YUV/GPUTexture
// 暂时限定一路音频与一路视频，后续扩展多路
// 音频与视频来了就播，同步是数据源已经做了的，主要是渲染原始数据
// 可以看做是一个简化版的播放器
// 还是需要把打开/关闭做到单独线程队列中
// 避免在本身线程回调又调用相应操作,导致锁状态不对。
class SourcePlayer : public ISourcePlayer,
                     public BasePlayer,
                     public IRawSourceOb,
                     public IMuxerOb,
                     public ISurfaceRenderOb,
                     public RunTask {
 public:
  SourcePlayer();
  virtual ~SourcePlayer();

 protected:
  bool initSource();

 protected:
  // 根据sourceType创建的RawSource
  std::unique_ptr<DeviceSource> source = nullptr;
  // 把source编码后封装复用到文件或是网络流
  std::unique_ptr<RawMuxer> rawMuxer = nullptr;
  // 视频渲染到窗口
  std::unique_ptr<WindowRender> windowRender = nullptr;
  // 音频渲染
  std::unique_ptr<AudioOutput> audioRender = nullptr;
  // 字幕显示
  std::unique_ptr<SubtitleView> subtitleView;
  // 是否渲染音频，有时做测试，需要关闭播放器本身的音频渲染
  bool bAudioRender = false;
  // rawmuxer记录时,windows平台需要输出YUV
  bool bMuxerCpu = false;

 private:
  void cmdOpen();
  void cmdReady();
  void cmdClose();

 protected:
  // 播放器线程
  virtual void onRunTask() override;

 public:
  virtual ISurfaceRender* getSurfaceRender() override;
  virtual IAudioRender* getAudioRender() override;
  virtual ISubtitle* getSubtitle() override;
  virtual void setAudioSource(IAudioSource* source) override;
  virtual void setVideoSource(IVideoSource* source) override;
  virtual IMediaMuxer* getMuxer() override;
  virtual bool open() override;
  virtual void close() override;
  virtual PlayerState getState() override;
  virtual ISourceInfo* getSourceInfo() override;

  // IMuxerOb
 public:
  virtual void onMuxerOpen(MediaMuxer* muxer) override;
  virtual void onMuxerClose() override;

  // IRawSourceOb
 public:
  virtual void onReady() override;
  virtual void onError(AVError error, const char* msg) override;
  virtual void onVideoFrame(const YUVFrame& frame,
                            int32_t trackId = 0) override;
  virtual void onGpuFrame(const GpuFrame& frame, int32_t trackId = 0) override;
  virtual void onAudioFrame(const AvoxAFrame& frame,
                            int32_t trackId = 0) override;
  virtual void onClose() override;
};

}