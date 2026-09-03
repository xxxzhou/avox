#pragma once

#include "AudioSource.hpp"
#include "RawSource.hpp"
#include "VideoSource.hpp"

namespace avox {

// 组合AudioSource和VideoSource,或其中一种
// 设备直出YUV/PCM的媒体源
class DeviceSource : public RawSource,
                     public IAudioSourceOb,
                     public IVideoSourceOb {
public:
  DeviceSource();
  virtual ~DeviceSource() {}

protected:
  // 音频源
  AudioSource *audioSource = nullptr;
  // 视频源
  VideoSource *videoSource = nullptr;

public:
  void enableDefaultAudioSource();
  void enableDefaultVideoSource();
  // 音频设备有没,会起到类似BaseSource::disableVideo的作用
  void setAudioSource(IAudioSource *source);
  // 视频设备有没,会起到类似BaseSource::disableVideo的作用
  void setVideoSource(IVideoSource *source);

private:
  void openAudioSource();
  void openVideoSource();
  void closeAudioSource();
  void closeVideoSource();

  // IRawSource
public:
  // 子类source在调用open后调用
  virtual bool open() override;
  virtual void close() override;
  virtual bool bOpening() override;

  // IAudioSourceOb
public:
  virtual void onAudioDesc(const AudioDesc &desc) override;
  virtual void onAudioError(AVError error, const char *msg) override;
  virtual void onAudioFrame(const AvoxAFrame &frame) override;
  virtual void onAudioClose() override;

  // IVideoSourceOb
public:
  virtual void onVideoDesc(const VideoDesc &desc) override;
  virtual void onVideoError(AVError error, const char *msg) override;
  virtual void onVideoFrame(const YUVFrame &frame) override;
  virtual void onGpuFrame(const GpuFrame &frame) override;
  virtual void onVideoClose() override;
};

}