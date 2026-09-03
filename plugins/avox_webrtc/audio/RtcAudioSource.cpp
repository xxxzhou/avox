#include "RtcAudioSource.hpp"

namespace avox {

RtcAudioSource::RtcAudioSource() {}

RtcAudioSource::~RtcAudioSource() {}

void RtcAudioSource::setSource(IAudioSource* source_) {
  AudioSource* xsource = dynamic_cast<AudioSource*>(source_);
  if (xsource == source) {
    return;
  }
  if (source) {
    source->close();
    source->removeObserver(this);
  }
  source = xsource;
  if (source) {
    source->setObserver(this);
    if (!sinks.empty()) {
      source->open();
    }
  }
}

void RtcAudioSource::close() {
  {
    std::lock_guard<std::mutex> lock(sinkLock);
    if (source) {
      source->close();
      source->removeObserver(this);            
    }
    sinks.clear();
  }
  sstate = SourceState::kEnded;
  // 通知 WebRTC 框架该 Source 状态已改变（基类方法）
// FireOnChanged();
  LOGFLF(LogLevel::info, "rtc audio source closed");
}

void RtcAudioSource::onAudioDesc(const AudioDesc& desc) {
  srcDesc = desc;
  outDesc = desc;
  // 转化成WebRTC音频源需要的格式
  if (desc.sampleRate == 44100) {
    outDesc.sampleRate = 48000;
  }
  if (desc.format != AudioFormat::AVOX_AUDIO_S16) {
    outDesc.format = AudioFormat::AVOX_AUDIO_S16;
  }
  if (desc.channels > 2) {
    outDesc.channels = 2;
  }
  if (initConfig(srcDesc, outDesc)) {
    frameSize = getAudioFrameSize(outDesc, frameMs);
  } else {
    LOGFLF(LogLevel::warn, "initConfig failed");
  }
}
void RtcAudioSource::onAudioError(AVError error, const char* msg) {}

void RtcAudioSource::onAudioFrame(const AvoxAFrame& frame) {
  // 由AudioReshaper重采样及分隔固定时间音频
  process(frame);
}

void RtcAudioSource::onProcess() {
  std::lock_guard<std::mutex> lock(sinkLock);
  if (sinks.empty()) {
    return;
  }
  const uint8_t* data = curFrame.point();
  size_t samples = getSamples(curFrame.getSize(), outDesc);
  for (auto* sink : sinks) {
    sink->OnData(data, 16, outDesc.sampleRate, outDesc.channels, samples);
  }
}

void RtcAudioSource::onAudioClose() {}

void RtcAudioSource::AddSink(webrtc::AudioTrackSinkInterface* sink) {
  std::lock_guard<std::mutex> lock(sinkLock);
  if (source && sinks.empty()) {
    source->setObserver(this);
    source->open();
    sstate = SourceState::kLive;
  }
  sinks.insert(sink);
}

void RtcAudioSource::RemoveSink(webrtc::AudioTrackSinkInterface* sink) {
  std::lock_guard<std::mutex> lock(sinkLock);
  sinks.erase(sink);
  if (source && sinks.empty()) {
    source->close();
    source->removeObserver(this);
    sstate = SourceState::kEnded;
  }
}

}
