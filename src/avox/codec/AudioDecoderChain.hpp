#pragma once

#include <vector>

#include "../audio/AudioDecoder.hpp"
#include "CodecChain.hpp"

namespace avox {

// 音频解码候选链: 比视频那条薄得多 —— 音频没有硬/软两极、没有 vulkan 备选、
// 没有关键帧落点与首帧看门狗, 只有"优先名提示 + 注册表其余"
class AudioDecoderChain : public CodecChain<ACodecId, AudioDecoder, ACodecDesc> {
 public:
  AudioDecoderChain() = default;
  virtual ~AudioDecoderChain() = default;

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
