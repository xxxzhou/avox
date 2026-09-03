#pragma once

#include "../AvoxPlayer.h"
#include "../module/JsonOption.hpp"
#include "../source/AVSource.hpp"
#include "MediaMuxer.hpp"

namespace avox {

// 流录制器：将网络流或本地文件转封装保存到本地
// 不涉及解码编码，纯转封装，速度取决于IO
class StreamRecorder : public IRecorder,
                       public JsonOption,
                       public IAVSourceOb,
                       public Observer<IRecorderOb> {
 public:
  StreamRecorder();
  virtual ~StreamRecorder();

 protected:
  std::unique_ptr<AVSource> source;
  std::unique_ptr<MediaMuxer> muxer;
  RecorderState state = RecorderState::none;
  IoPlan ioPlan = IoPlan::ffmpeg;
  MuxerType muxerType = MuxerType::ffmpeg;
  std::string inputUrl;
  std::string outputFile;
  RecorderProgress progress = {};
  // 源是否有视频轨(决定进度用视频还是音频PTS)
  bool bHaveVideo = false;
  // 视频编码,none=丢弃视频轨
  VCodecId vcodecId = VCodecId::h265;
  // 音频编码,none=丢弃音频轨
  ACodecId aCodecId = ACodecId::aac;

  // IRecorder
 public:
  virtual void setIoPlan(IoPlan plan) override;
  virtual void setMuxerType(MuxerType type) override;
  virtual void setVideoCodec(VCodecId codecId) override;
  virtual void setAudioCodec(ACodecId codecId) override;
  virtual ISurfaceRender* getSurfaceRender() override {return nullptr;}
  virtual IAudioRender* getAudioRender() override { return nullptr; }
  virtual bool open(const char* inputUrl, const char* outputFile) override;
  virtual void close() override;
  virtual RecorderState getState() override { return state; }
  // 返回内置的参数设置器(open前设置生效)
  virtual IOption* getOption() override { return this; }

 private:
  void closeMuxer();
  void setRecState(RecorderState newState);

  // IAVSourceOb
 public:
  virtual void onReady() override;
  virtual void onSyncPts() override {}
  virtual void onClose() override;
  virtual void onComplete() override;
  virtual void onError(AVError error, const char* msg) override;
  virtual void onPacket(const AvoxPacket& packet) override;
};

}
