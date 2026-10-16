#include "AudioStream.hpp"

#include "../module/AvoxManager.hpp"
#include "RawMuxer.hpp"

namespace avox {

AudioStream::AudioStream() {}

AudioStream::~AudioStream() { stopEncoder(); }

void AudioStream::stopEncoder() {
  if (encoder()) {
    // 排空: 音频编码器内部线程的待出包在 sink 尚活时落盘
    encoder()->flush();
  }
  encoderChain.reset(*this);
  muxer = nullptr;
}

ATrackDesc AudioStream::setAudioDesc(const ATrackDesc& desc_,
                                     const AudioDesc& outDesc_) {
  desc = desc_;
  wantDesc = outDesc_;
  // 协商不出就用源格式(与旧行为一致: 失败路径返回入参)
  outDesc = desc;
  // 候选 = 优先名提示 → 注册表全量(与视频编码腿同一份机械逻辑)
  if (!encoderChain.build(desc.codecId)) {
    LOGFLF(LogLevel::warn, "not find encoder codecId:",
           getACodecName(desc.codecId));
    return desc;
  }
  if (!encoderChain.selectFirst(*this)) {
    LOGFLF(LogLevel::warn, "no encoder lane available:",
           getACodecName(desc.codecId));
    return desc;
  }
  log(LogLevel::info, "audio stream in: ", desc, " out: ", outDesc);
  return outDesc;
}

// 车道接线: 设定想要的输出格式, 取回编码器实际接受的输出格式, 再绑观察者
bool AudioStream::laneInit(AudioEncoder& impl, const ACodecDesc&) {
  impl.setOutDesc(wantDesc);
  outDesc = impl.setDesc(desc);
  impl.setObserver(this);
  return true;
}

void AudioStream::laneClose(AudioEncoder& impl) { impl.removeObserver(this); }

void AudioStream::laneSaveConfigs(AudioEncoder& oldImpl) {}

void AudioStream::laneChanged(AudioEncoder& newImpl, bool bFirst) {
  bWarnedEncOpenFail = false;
  if (bFirst) {
    return;
  }
  LOGFLF(LogLevel::warn, "audio encoder fallback to:", encoderChain.implName());
}

void AudioStream::encoderFrame(const AvoxAFrame& frame) {
  if (!encoder()) {
    // LOGFLF(LogLevel::warn, "encoder is null");
    return;
  }
  // 音频不换道: 封装侧音频轨描述(采样率/声道/格式)是按当前编码器的协商结果定的,
  // 换一条编码器就可能与已写的头不符 —— 打不开只报一次, 别静默成"无声无日志"
  DecodeResult result = encoder()->encode(frame);
  if (result == DecodeResult::openFailed && !bWarnedEncOpenFail) {
    bWarnedEncOpenFail = true;
    LOGFLF(LogLevel::error, "audio encoder can not open:",
           encoderChain.implName(), " codec:", getACodecName(desc.codecId));
  }
}

void AudioStream::onPacket(AvoxPacket& packet) {
  if (!muxer) {
    LOGFLF(LogLevel::warn, "muxer is null");
    return;
  }
  muxer->pushPacket(packet);
}

}
