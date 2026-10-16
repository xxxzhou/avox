#include "AudioEncoderChain.hpp"

#include "../module/AvoxManager.hpp"

namespace avox {

const std::vector<AudioEncoderChain::Item>& AudioEncoderChain::laneItems() const {
  return AvoxManager::Get().aEncoders.initFuncs(codecId);
}

// 候选次序: 优先名提示 → 注册表其余(按注册表序)。
// 改造前 AudioStream 取的是"优先名命中, 否则下标 0"这一条; 注册表全量进候选后
// 与注册顺序无关, 且与视频编码腿口径一致
bool AudioEncoderChain::build(ACodecId codecId_) {
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
    pushLane(indexOf(AVOX_FF_FDKAAC_ENCODER));
  }
  for (size_t i = 0; i < items.size(); ++i) {
    pushLane((int32_t)i);
  }
  setCands(std::move(list));
  return !cands.empty();
}

}
