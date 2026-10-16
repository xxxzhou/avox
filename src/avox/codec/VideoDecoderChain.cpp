#include "VideoDecoderChain.hpp"

#include "../module/AvoxManager.hpp"
#include "../module/LogHelper.hpp"

namespace avox {

const std::vector<VideoDecoderChain::Item>& VideoDecoderChain::laneItems()
    const {
  return AvoxManager::Get().vDecoders.initFuncs(codecId);
}

namespace {

// vulkan 系车道名(整体排末位的那一族)
bool isVulkanLane(const std::string& name) {
  return name == AVOX_FFVULKAN_H264_DECODER ||
         name == AVOX_FFVULKAN_H265_DECODER ||
         name == AVOX_FFVULKAN_VP9_DECODER ||
         name == AVOX_FFVULKAN_AV1_DECODER;
}

}

// 候选次序(逐条对齐改造前的行为):
//   覆盖点名 → 偏好极提示名 → 另一极提示名 → 注册表其余(按注册表序)
//   vulkan 系整体排末位; 覆盖点名时不插 vulkan
// 与旧写法的两处差别都是要修的缺口:
//   ① 旧写法把"另一极"写死成 getDefaultDecoderName(codecId, !bHard), 软解偏好时
//      该值为 nullptr —— 于是"设了软解"根本没有兜底。现在另一极是提示名 + 注册表
//      全量, 恒存在
//   ② 旧写法在提示名未注册时落到"注册表首项"这一隐式侥幸; 现在注册表全量本来
//      就在候选表里, 该侥幸路径不存在
bool VideoDecoderChain::build(VCodecId codecId_, bool bPrefHard,
                              const std::string& overrideName) {
  codecId = codecId_;
  cands.clear();
  tried.clear();
  const auto& items = laneItems();
  if (items.empty()) {
    return false;
  }
  // 名字是全流程唯一的车道标识, 统一按名字回查注册表下标
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
  // 提示名: 偏好极与另一极各一个 —— 只决定同一极内谁先试
  const char* sHintPref = getDefaultDecoderName(codecId, bPrefHard);
  const char* sHintOther = getDefaultDecoderName(codecId, !bPrefHard);
  // 覆盖点名(排障/测试强制车道): 排首位, 且不再插 vulkan 备选。与软/硬偏好无关 ——
  // 点名本身就是最高优先级的意愿, 旧写法 `(bHard && !empty)` 会在"软解偏好 + 点名"
  // 时静默忽略它(既不生效也不报错), 点名的人只会看到"我指定了却没生效"
  const char* sOverride = overrideName.empty() ? nullptr : overrideName.c_str();
  if (sOverride && indexOf(sOverride) < 0) {
    LOGFLF(LogLevel::warn, "override decoder ", sOverride,
           " not registered, fall back to candidate chain");
  }
  // 原生 "av1" 是 hwaccel-only 包装(无 hwaccel 上下文即每包 ENOSYS), 构建带
  // dav1d 时注册表里已被 FFHelper 换名成真软解 libdav1d; 没带 dav1d 的构建里
  // 它还在注册表, 须整个剔除, 并让软侧上浮硬解(旧行为: 无 dav1d 时"软解设置"
  // 实际也走硬解)
  bool bDropAv1Shell = false;
  if (codecId == VCodecId::av1) {
    if (indexOf(AVOX_FF_LIBDAV1D_DECODER) >= 0) {
      if (bPrefHard) {
        sHintOther = AVOX_FF_LIBDAV1D_DECODER;
      } else {
        sHintPref = AVOX_FF_LIBDAV1D_DECODER;
      }
    } else {
      bDropAv1Shell = true;
      sHintOther = nullptr;
      if (!bPrefHard) {
        sHintPref = getDefaultDecoderName(codecId, true);
      }
    }
  }
  // vulkan 备选只对 h264/h265 生效(旧行为): Apple 无 vulkan 解码器注册, 其他
  // 平台 setContext→onVaild 要探一次运行时, 故整体排末位免白等
  const bool bUseVulkan =
      (codecId == VCodecId::h264 || codecId == VCodecId::h265) && !sOverride;
  std::vector<int32_t> vulkanLanes;
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
  pushLane(indexOf(sOverride));
  if (!sOverride) {
    pushLane(indexOf(sHintPref));
  }
  pushLane(indexOf(sHintOther));
  for (size_t i = 0; i < items.size(); ++i) {
    const std::string& name = items[i].desc.name;
    if (bDropAv1Shell && name == AVOX_FF_AV1_DECODER) {
      continue;
    }
    if (isVulkanLane(name)) {
      if (bUseVulkan) {
        vulkanLanes.push_back((int32_t)i);
      }
      continue;
    }
    pushLane((int32_t)i);
  }
  for (int32_t index : vulkanLanes) {
    pushLane(index);
  }
  setCands(std::move(list));
  return !cands.empty();
}

}
