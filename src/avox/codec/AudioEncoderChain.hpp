#pragma once

#include <vector>

#include "../audio/AudioEncoder.hpp"
#include "../player/AVDecoder.hpp"
#include "CodecChain.hpp"

namespace avox {

// 音频编码候选链: 与音频解码那条同形(无软/硬两极、无 vulkan),
// 候选 = 优先名提示 + 注册表其余
class AudioEncoderChain : public CodecChain<ACodecId, AudioEncoder, ACodecDesc> {
 public:
  AudioEncoderChain() = default;
  virtual ~AudioEncoderChain() = default;

 private:
  ACodecId codecId = ACodecId::none;

 protected:
  // 该编码注册表全量
  const std::vector<Item>& laneItems() const override;

 public:
  // 排候选表: 优先名(AAC→fdk-aac, 注册表里没有则跳过) → 注册表其余(按注册表序)
  bool build(ACodecId codecId);
  ACodecId getCodecId() const { return codecId; }
};

}
