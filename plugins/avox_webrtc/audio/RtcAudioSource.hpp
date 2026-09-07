#pragma once

#include "../RtcHelper.hpp"
#include "api/media_stream_interface.h"
#include "avox/audio/AudioReshaper.hpp"
#include "avox/source/AudioSource.hpp"
#include "pc/test/fake_audio_capture_module.h"

namespace avox {

class RtcAudioSource : public webrtc::AudioSourceInterface,
                       public AudioReshaper,
                       public IAudioSourceOb {
 public:
  RtcAudioSource();
  virtual ~RtcAudioSource();

 private:
  SourceState sstate = SourceState::kInitializing;
  std::mutex sinkLock;
  std::set<webrtc::AudioTrackSinkInterface*> sinks;
  // 项目已实现的音频源
  avox::AudioSource* source = nullptr;
  // 本地源描述
  AudioDesc adesc = {};

 public:
  void setSource(IAudioSource* source);
  void close();
  // 是否设置了推流源(决定open时是否AddTrack)
  bool hasSource();
  // 本地源描述(onAudioDesc后有效)
  AudioDesc getAudioDesc();

  // IAudioSourceOb
 public:
  virtual void onAudioDesc(const AudioDesc& desc) override;
  virtual void onAudioError(AVError error, const char* msg) override;
  virtual void onAudioFrame(const AvoxAFrame& frame) override;
  virtual void onAudioClose() override;

 protected:
  // 由AudioReshaper先重采样到webrtc格式
  // 再把音频数据切片成10ms一帧
  virtual void onProcess() override;

 public:
  virtual void AddSink(webrtc::AudioTrackSinkInterface* sink) override;
  virtual void RemoveSink(webrtc::AudioTrackSinkInterface* sink) override;
  virtual SourceState state() const override { return sstate; };
  virtual bool remote() const override { return false; };
  void RegisterObserver(webrtc::ObserverInterface* observer) override {}
  void UnregisterObserver(webrtc::ObserverInterface* observer) override {}
};

}
