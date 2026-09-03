#include "AudioStream.hpp"

#include "../module/AvoxManager.hpp"
#include "RawMuxer.hpp"

namespace avox {

AudioStream::AudioStream() {}

AudioStream::~AudioStream() {}

ATrackDesc AudioStream::setAudioDesc(const ATrackDesc& desc_,
                                     const AudioDesc& outDesc_) {
  desc = desc_;
  bool bFind = AvoxManager::Get().aEncoders.hasObjectId(desc.codecId);
  if (!bFind) {
    LOGFLF(LogLevel::warn, "not find codecId:", getACodecName(desc.codecId));
    return desc;
  }
  const auto& encodes = AvoxManager::Get().aEncoders.initFuncs(desc.codecId);
  if (encodes.empty()) {
    LOGFLF(LogLevel::warn, "not find decoder:", getACodecName(desc.codecId));
    return desc;
  }
  // 根据播放器设置选择解码器
  size_t sIndex = 0;
  if (desc.codecId == ACodecId::aac) {
    // ffmpeg使用aac编码，编码出来固定fltp,不太实用
    // 优先fdk-aac，其使用cmake方便编译到各平台
    const char* sName = "fdk-aac encoder";
    // 查找解码器
    for (size_t i = 0; i < encodes.size(); ++i) {
      if (encodes[i].desc.name == sName) {
        sIndex = i;
        break;
      }
    }
  }
  auto& aEncode = encodes[sIndex];
  // 初始化解码器
  encoder = std::unique_ptr<AudioEncoder>(aEncode.initFunc());
  if (!encoder) {
    LOGFLF(LogLevel::warn, "init encoder failed:", aEncode.desc.name);
    return desc;
  }
  // 设定想要的输出格式,编码器不一定支持
  encoder->setOutDesc(outDesc_);
  // 得到最终的输出格式
  outDesc = encoder->setDesc(desc);
  log(LogLevel::info, "audio stream in: ", desc, " out: ", outDesc);
  encoder->setObserver(this);
  return outDesc;
}

void AudioStream::encoderFrame(const AvoxAFrame& frame) {
  if (!encoder) {
    // LOGFLF(LogLevel::warn, "encoder is null");
    return;
  }
  encoder->encode(frame);
}

void AudioStream::onPacket(AvoxPacket& packet) {
  if (!muxer) {
    LOGFLF(LogLevel::warn, "muxer is null");
    return;
  }
  muxer->pushPacket(packet);
}

}
