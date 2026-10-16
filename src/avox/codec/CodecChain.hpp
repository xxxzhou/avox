#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "../AvoxCodec.h"
#include "../AvoxDef.h"
#include "../module/RegeditObj.hpp"

namespace avox {

// 解码器注册名(FF 项即 FFmpeg 侧 codec->name)。历史误写 libx264/libx265 ——
// 那是编码器名, 不存在同名解码器。软解项用 #ifndef 保护, 允许构建期覆盖。
#ifndef AVOX_FF_H264_DECODER
#define AVOX_FF_H264_DECODER "h264"
#endif
#ifndef AVOX_FF_H265_DECODER
#define AVOX_FF_H265_DECODER "hevc"
#endif
#ifndef AVOX_FF_VP9_DECODER
#define AVOX_FF_VP9_DECODER "vp9"
#endif
#ifndef AVOX_FF_AV1_DECODER
#define AVOX_FF_AV1_DECODER "av1"
#endif
// 真软解 AV1(FFmpeg libdav1d 包装): 构建带 dav1d 时软解名指它 —— 原生 "av1"
// 是 hwaccel-only 包装(无 hwaccel 即 ENOSYS), 名与 FFmpeg 注册名一致故
// FFVDecoder 按名可查(1003 缩略图风暴定修)
#ifndef AVOX_FF_LIBDAV1D_DECODER
#define AVOX_FF_LIBDAV1D_DECODER "libdav1d"
#endif
#ifndef AVOX_FF_PRORES_DECODER
// = FFmpeg 侧注册名(regFFCodec 用 codec->name), 与 AVOX_FF_VP9_DECODER 同款约定
#define AVOX_FF_PRORES_DECODER "prores"
#endif
#ifndef AVOX_FF_VVC_DECODER
#define AVOX_FF_VVC_DECODER "vvc"
#endif
#define AVOX_ANDROID_H264_DECODER "android h264 decoder"
#define AVOX_ANDROID_H265_DECODER "android h265 decoder"
#define AVOX_ANDROID_VP9_DECODER "android vp9 decoder"
#define AVOX_ANDROID_AV1_DECODER "android av1 decoder"
#define AVOX_IOS_H264_DECODER "ios h264 decoder"
#define AVOX_IOS_H265_DECODER "ios h265 decoder"
#define AVOX_IOS_VP9_DECODER "ios vp9 decoder"
#define AVOX_IOS_AV1_DECODER "ios av1 decoder"
#define AVOX_IOS_PRORES_DECODER "ios prores decoder"
#define AVOX_FFVULKAN_H264_DECODER "ff_h264_vulkan"
#define AVOX_FFVULKAN_H265_DECODER "ff_hevc_vulkan"
#define AVOX_FFVULKAN_VP9_DECODER "ff_vp9_vulkan"
#define AVOX_FFVULKAN_AV1_DECODER "ff_av1_vulkan"
#define AVOX_FFDX11_H264_DECODER "ff_h264_dx11"
#define AVOX_FFDX11_H265_DECODER "ff_hevc_dx11"
#define AVOX_FFDX11_VP9_DECODER "ff_vp9_dx11"
#define AVOX_FFDX11_AV1_DECODER "ff_av1_dx11"
#define AVOX_FFVAAPI_H264_DECODER "ff_h264_vaapi"
#define AVOX_FFVAAPI_H265_DECODER "ff_hevc_vaapi"
// 音频解码器优先名: AAC 走 fdk-aac(与 FFmpeg 内置 aac 的输出格式/多声道行为不同,
// 历史选择)。注册名见 avox_aac/FdkaacDecoder.cpp
#define AVOX_FF_FDKAAC_DECODER "fdk-aac decoder"

// 编码器注册名 —— 与解码侧同为"注册表里的名字", 两边的名字都只在本文件维护。
// 软编名构建期可注入(CMake AVOX_DIST_FLAVOR): agpl 渠道默认 libx264/libx265(GPL);
// commercial(LGPL) 渠道按平台注入: Windows=h264_mf/hevc_mf, Apple=h264/hevc_videotoolbox,
// Android=AndVEncoder 原生硬编注册名(Android LGPL 包无视频软编)
#ifndef AVOX_FF_H264_ENCODER
#define AVOX_FF_H264_ENCODER "libx264"
#endif
#ifndef AVOX_FF_H265_ENCODER
#define AVOX_FF_H265_ENCODER "libx265"
#endif
#define AVOX_ANDROID_H264_ENCODER "android h264 decoder"
#define AVOX_ANDROID_H265_ENCODER "android h265 decoder"
#define AVOX_IOS_H264_ENCODER "ios h264 decoder"
#define AVOX_IOS_H265_ENCODER "ios h265 decoder"
#define AVOX_FFVULKAN_H264_ENCODER "ff_h264_vulkan"
#define AVOX_FFVULKAN_H265_ENCODER "ff_hevc_vulkan"
#define AVOX_FFDX11_H264_ENCODER "ff_h264_dx11"
#define AVOX_FFDX11_H265_ENCODER "ff_hevc_dx11"
#define AVOX_FFVAAPI_H264_ENCODER "ff_h264_vaapi"
#define AVOX_FFVAAPI_H265_ENCODER "ff_hevc_vaapi"
// 音频编码器优先名: AAC 走 fdk-aac(FFmpeg 内置 aac 编码输出固定 fltp, 不实用;
// fdk 用 cmake 也好编到各平台)。不是软/硬极, 只是候选排序里的优先级提示
#define AVOX_FF_FDKAAC_ENCODER "fdk-aac encoder"

// 平台/编码的默认解码器名。**只是候选排序里的优先级提示**(同一软硬极内谁先试),
// 不再是"软/硬极点" —— 极由注册表的 bHardware 决定(见 CodecChain),
// 因此本表给不出某个编码的软解名也不会退化成"拿 h264 的名字兜底"。
AVOX_EXPORT const char* getDefaultDecoderName(VCodecId codecId, bool bHard);

// 候选链: 把「选哪个实现 / 换下一个」收成一份, 供各条腿共用。
// - 候选 = 该编码注册表的**全量**, 上层偏好只决定谁排在前面 —— 不假定上层
//   已设定软或硬, 因此"设了软解"同样有另一极兜底(旧写法写死另一极且软解侧
//   为 nullptr, 于是软解偏好根本没有回退)
// - 只做媒体/阶段无关的机械动作: 逐个试、已试记账、换实例、回调宿主
// - 排序策略与宿主反应由具体媒体类型/腿给出(见 VideoDecoderChain)
template <typename TTypeId, typename TImpl, typename TDesc>
class CodecChain {
 public:
  using Registry = RegeditObjList<TTypeId, TImpl, TDesc>;
  using Item = typename Registry::ObjectInfo;

  // 一个候选。只记注册表下标, 不拷 initFunc —— std::function 的 type-erasure
  // manager 实例化在注册点(可能是插件 dll), 拷到别处会在插件卸载后悬垂
  struct Cand {
    std::string name;
    size_t index = 0;
  };

  // 宿主: 每条腿实现一次, 只做候选链不知道的事
  class Host {
   public:
    virtual ~Host() = default;
    // 建实例后接线(linkOption/setObserver/setDvProfile)并 setContext;
    // 返回 false 表示该候选不可用, 换下一条
    virtual bool laneInit(TImpl& impl, const TDesc& desc) = 0;
    // 实例即将销毁: 解绑(removeObserver)
    virtual void laneClose(TImpl& impl) = 0;
    // 换道: 从旧车道保存配置帧供新车道重喂(不换道不调)
    virtual void laneSaveConfigs(TImpl& oldImpl) = 0;
    // 换道成功: 重喂配置帧 + 本腿副作用(吸关键帧/看门狗重计时/埋点)
    virtual void laneChanged(TImpl& newImpl, bool bFirst) = 0;
  };

 protected:
  // 待试候选(按序)
  std::vector<Cand> cands;
  // 已试车道名, 同一条不重试
  std::vector<std::string> tried;
  // 当前车道
  std::unique_ptr<TImpl> decode;
  std::string laneName;
  // 注册表全量, 由具体媒体类型提供
  virtual const std::vector<Item>& laneItems() const = 0;

 private:
  bool trySelect(const Cand& cand, Host& host, bool bFirst);

 public:
  virtual ~CodecChain() = default;
  // 候选表由具体媒体类型按策略灌入
  void setCands(std::vector<Cand> list) { cands = std::move(list); }
  // 清候选/已试/实例(有活实例时先解绑)
  void reset(Host& host);
  // 首次选型: 按候选表逐个建实例, 成功即停
  bool selectFirst(Host& host);
  // 运行期换道(openFailed/首帧看门狗超时): 试下一条未试过的候选
  bool selectNext(Host& host);
  bool hasNext() const;
  bool laneTried(const std::string& name) const;
  TImpl* impl() { return decode.get(); }
  const std::string& implName() const { return laneName; }
};

template <typename TTypeId, typename TImpl, typename TDesc>
bool CodecChain<TTypeId, TImpl, TDesc>::laneTried(
    const std::string& name) const {
  for (const auto& n : tried) {
    if (n == name) {
      return true;
    }
  }
  return false;
}

template <typename TTypeId, typename TImpl, typename TDesc>
void CodecChain<TTypeId, TImpl, TDesc>::reset(Host& host) {
  cands.clear();
  tried.clear();
  if (decode) {
    host.laneClose(*decode);
    decode.reset();
  }
  laneName.clear();
}

template <typename TTypeId, typename TImpl, typename TDesc>
bool CodecChain<TTypeId, TImpl, TDesc>::hasNext() const {
  for (const auto& cand : cands) {
    if (!laneTried(cand.name)) {
      return true;
    }
  }
  return false;
}

template <typename TTypeId, typename TImpl, typename TDesc>
bool CodecChain<TTypeId, TImpl, TDesc>::trySelect(const Cand& cand, Host& host,
                                                  bool bFirst) {
  const auto& items = laneItems();
  if (cand.index >= items.size()) {
    return false;
  }
  const Item& item = items[cand.index];
  auto impl = std::unique_ptr<TImpl>(item.initFunc());
  if (!impl) {
    tried.push_back(cand.name);
    return false;
  }
  // 接线与 setContext 都算"这条车道能不能用"; 旧车道这时还活着(与旧行为一致:
  // 候选全试完才判车道死, 中途失败不动在用的那条)
  if (!host.laneInit(*impl, item.desc)) {
    tried.push_back(cand.name);
    return false;
  }
  if (decode) {
    host.laneSaveConfigs(*decode);
    host.laneClose(*decode);
  }
  decode = std::move(impl);
  laneName = cand.name;
  tried.push_back(cand.name);
  host.laneChanged(*decode, bFirst);
  return true;
}

template <typename TTypeId, typename TImpl, typename TDesc>
bool CodecChain<TTypeId, TImpl, TDesc>::selectFirst(Host& host) {
  for (const auto& cand : cands) {
    if (laneTried(cand.name)) {
      continue;
    }
    if (trySelect(cand, host, /*bFirst=*/true)) {
      return true;
    }
  }
  return false;
}

template <typename TTypeId, typename TImpl, typename TDesc>
bool CodecChain<TTypeId, TImpl, TDesc>::selectNext(Host& host) {
  for (const auto& cand : cands) {
    if (laneTried(cand.name)) {
      continue;
    }
    if (trySelect(cand, host, /*bFirst=*/false)) {
      return true;
    }
  }
  return false;
}

}
