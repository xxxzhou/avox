#pragma once

#include <string>
#include <vector>

#include "../player/AVDecoder.hpp"
#include "../video/VideoEncoder.hpp"
#include "CodecChain.hpp"

namespace avox {

// 视频编码候选链: 机械部分与解码侧同构(注册表逐个试 + 已试记账 + 换实例 + 宿主
// 回调), 差别在宿主响应 —— 编码器"打不开"发生在首帧(onPreEncoder)而非建实例时,
// 故 laneInit(建实例+setDesc)恒能成功, 真正的换道由腿在 encode() 返回 openFailed
// 时发起(此时封装头未定, 换同编码的另一实现是安全的)
class VideoEncoderChain : public CodecChain<VCodecId, VideoEncoder, VCodecDesc> {
 public:
  VideoEncoderChain() = default;
  virtual ~VideoEncoderChain() = default;

 private:
  VCodecId codecId = VCodecId::none;

 protected:
  // 该编码注册表全量
  const std::vector<Item>& laneItems() const override;

 public:
  // 排候选表: 偏好极默认编码器名(未注册则跳过) → 注册表其余(按注册表序)。
  // bPrefHard 只决定谁先试, 另一极恒在候选表里 —— 不假定上层已设定软/硬
  bool build(VCodecId codecId, bool bPrefHard);
  VCodecId getCodecId() const { return codecId; }
};

}
