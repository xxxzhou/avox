#include "RtcAudioProcess.hpp"

#include "api/audio/builtin_audio_processing_builder.h"
#include "api/environment/environment_factory.h"

namespace avox {

using namespace webrtc;

RtcAudioProcess::RtcAudioProcess() {
  enableAec = true;
  enableNs = true;
  enableAgc = true;
}

RtcAudioProcess::~RtcAudioProcess() {
  // if (apm) {
  //   apm.reset();
  // }
}

bool RtcAudioProcess::onInit() {
  outDesc = srcDesc;
  // 配置WebRTC音频处理流的格式
  if (srcDesc.sampleRate == 44100) {
    outDesc.sampleRate = 48000;
  }
  if (srcDesc.format != AudioFormat::AVOX_AUDIO_S16) {
    outDesc.format = AudioFormat::AVOX_AUDIO_S16;
  }
  if (srcDesc.channels > 2) {
    outDesc.channels = 2;
  }
  
  AudioProcessing::Config config;
  // 配置回声消除
  config.echo_canceller.enabled = enableAec;
  config.echo_canceller.mobile_mode = false;
  // 配置噪声抑制
  config.noise_suppression.enabled = enableNs;
  config.noise_suppression.level =
      AudioProcessing::Config::NoiseSuppression::kHigh;
  // 配置自动增益控制
  config.gain_controller2.enabled = enableAgc;
  // 测试 声音增大二倍
  // config.gain_controller2.fixed_digital.gain_db = 10.0f;
  apm = BuiltinAudioProcessingBuilder(config).Build(CreateEnvironment());
  if (!apm) {
    LOGFLF(LogLevel::warn, "failed to create WebRTC AudioProcessing instance");
    return false;
  }
  // 输入流配置
  pConfig.streams[0] = StreamConfig(outDesc.sampleRate, outDesc.channels);
  // 输出流配置
  pConfig.streams[1] = StreamConfig(outDesc.sampleRate, outDesc.channels);
  if (apm->Initialize(pConfig) != 0) {
    LOGFLF(LogLevel::warn,
           "failed to initialize WebRTC AudioProcessing with sample rate:",
           srcDesc.sampleRate);
    return false;
  }
  return true;
}

void RtcAudioProcess::onProcess() {
  if (!apm) {
    return;
  }
  int32_t ret = apm->ProcessStream(
      (const int16_t*)curFrame.point(), pConfig.input_stream(),
      pConfig.output_stream(), (int16_t*)pbuffer.data());
  if (ret < 0) {
    LOGFLF(LogLevel::warn, "WebRTC AudioProcessing process failed");
    return;
  }
  AvoxAFrame frame = {};
  frame.buffer = {pbuffer.data(), (int32_t)pbuffer.size(), true};
  frame.pts = curFrame.getPts();
  dispatch(&IAudioProcessOb::onAudioProcess, frame);
}

}
