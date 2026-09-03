#pragma once

#include "../RtcHelper.hpp"
#include "api/audio/audio_processing.h"
#include "avox/audio/AudioProcess.hpp"

namespace avox {

// 具体应用WebRTC的3A处理
class RtcAudioProcess : public AudioProcess {
public:
  RtcAudioProcess();
  virtual ~RtcAudioProcess();

private:
  webrtc::scoped_refptr<webrtc::AudioProcessing> apm = nullptr;
  webrtc::ProcessingConfig pConfig = {};

public:
  virtual bool onInit() override;

protected:
  virtual void onProcess() override;
};

}
