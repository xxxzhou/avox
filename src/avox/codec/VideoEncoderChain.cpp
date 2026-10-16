#include "VideoEncoderChain.hpp"

#include "../module/AvoxManager.hpp"
#include "../module/LogHelper.hpp"
#include "../muxer/Muxer.hpp"

namespace avox {

const std::vector<VideoEncoderChain::Item>& VideoEncoderChain::laneItems() const {
  return AvoxManager::Get().vEncoders.initFuncs(codecId);
}

// 候选次序: 偏好极默认编码器名 → 注册表其余(按注册表序)。
// 改造前 VideoStream 只取一条: 按名命中就用它, 未命中就落到 encodes[0] —— 构建里
// 没有该硬编、该硬编建得起来但开不了, 两类都无路可退。注册表全量进候选后, 前者由
// 名次自然跳过(不占已试额度), 后者由腿在首帧 openFailed 时 selectNext
bool VideoEncoderChain::build(VCodecId codecId_, bool bPrefHard) {
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
  const char* sHint = getDefaultEncoderName(codecId, bPrefHard);
  if (indexOf(sHint) < 0) {
    LOGFLF(LogLevel::warn, "default encoder not registered:", sHint,
           " codec:", getVCodecName(codecId), " use registry order");
  }
  pushLane(indexOf(sHint));
  for (size_t i = 0; i < items.size(); ++i) {
    pushLane((int32_t)i);
  }
  setCands(std::move(list));
  return !cands.empty();
}

}
