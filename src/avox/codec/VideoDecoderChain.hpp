#pragma once

#include <string>
#include <vector>

#include "../video/VideoDecoder.hpp"
#include "CodecChain.hpp"

namespace avox {

// 视频解码候选链: 候选次序策略只此一份(播放腿/抽帧腿/WebRTC 收流共用同一个
// 类定义, 各自一个实例)
class VideoDecoderChain : public CodecChain<VCodecId, VideoDecoder, VCodecDesc> {
 public:
  VideoDecoderChain() = default;
  virtual ~VideoDecoderChain() = default;

 private:
  VCodecId codecId = VCodecId::none;

 protected:
  // 该编码注册表全量
  const std::vector<Item>& laneItems() const override;

 public:
  // 排候选表: 覆盖点名 → 偏好极提示名 → 另一极提示名 → 注册表其余(全量兜底)。
  // bPrefHard 只是"先试哪一极", 另一极与注册表其余一律排在后面 —— 因此不假定
  // 上层已设定软/硬, 设了软解也有另一极兜底
  bool build(VCodecId codecId, bool bPrefHard, const std::string& overrideName);
  VCodecId getCodecId() const { return codecId; }
};

}
