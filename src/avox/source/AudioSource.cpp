#include "AudioSource.hpp"

#include "../module/LogHelper.hpp"
namespace avox {

AudioSource::AudioSource() {
  // 子类根据设备返回相应的desc
  desc.format = AudioFormat::AVOX_AUDIO_S16;
  desc.sampleRate = 16000;
  desc.channels = 2;
  // 是否添加音频处理
  enableProcess = false;
}

void AudioSource::onFrame(const AvoxAFrame& frame) {
  if (!bFirstFrame) {
    initProcess = false;
    if (enableProcess) {
      audioProcess = std::unique_ptr<AudioProcess>(createWebRtcAudioProcess());
      if (audioProcess) {
        initProcess = audioProcess->init(desc);
        if (initProcess) {
          audioProcess->setObserver(this);
        }
      }
    }    
    dispatch(&IAudioSourceOb::onAudioDesc, desc);    
    bFirstFrame = true;
    LOGFLF(LogLevel::info, "initProcess:", initProcess, " desc: ", desc);
  }
  if (initProcess) {
    audioProcess->process(frame);
  } else {
    dispatch(&IAudioSourceOb::onAudioFrame, frame);
  }
}

void AudioSource::onAudioProcess(const AvoxAFrame& frame) {
  dispatch(&IAudioSourceOb::onAudioFrame, frame);
}

}
