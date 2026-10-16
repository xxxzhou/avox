#include "AudioDecoderChain.hpp"

#include "../module/AvoxManager.hpp"

namespace avox {

const std::vector<AudioDecoderChain::Item>& AudioDecoderChain::laneItems() const {
  return AvoxManager::Get().aDecoders.initFuncs(codecId);
}

// 候选次序: 优先名提示 → 注册表其余(按注册表序)。
// 旧写法(ADecoderTask `fallbacks`)取的是"选中项**之后**的注册后继", 于是兜底存在
// 与否取决于注册顺序 —— fdk-aac 若注册在末尾, 该编码就一条兜底都没有; 改成注册表
// 全量后与注册顺序无关, 而且三条腿(AudioTrack 播放/抽帧转码/WebRTC 收流)口径一致
bool AudioDecoderChain::build(ACodecId codecId_) {
  codecId = codecId_;
  cands.clear();
  tried.clear();
  const auto& items = laneItems();
  if (items.empty()) {
    return false;
  }
  const auto indexOf = [&items](const char* name) -> int32_t {
    if (!name || !*name) {
      return -1;
    }
    for (size_t i = 0; i < items.size(); ++i) {
      if (items[i].desc.name == name) {
        return (int32_t)i;
      }
    }
    return -1;
  };
  std::vector<Cand> list;
  const auto pushLane = [&list, &items](int32_t index) {
    if (index < 0) {
      return;
    }
    const std::string& name = items[index].desc.name;
    for (const auto& cand : list) {
      if (cand.name == name) {
        return;
      }
    }
    list.push_back({name, static_cast<size_t>(index)});
  };
  if (codecId == ACodecId::aac) {
    pushLane(indexOf(AVOX_FF_FDKAAC_DECODER));
  }
  for (size_t i = 0; i < items.size(); ++i) {
    pushLane((int32_t)i);
  }
  setCands(std::move(list));
  return !cands.empty();
}

}
