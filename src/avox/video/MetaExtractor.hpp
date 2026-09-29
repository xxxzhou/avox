#pragma once

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

#include "../AvoxCodec.h"
#include "../AvoxVideo.h"
#include "../source/PacketBuf.hpp"

namespace avox {

// 视频元数据提取器(解码器无关)
//
// 从码流里抠 HDR 静态元数据(SEI 137/144)与 Dolby Vision RPU(NAL 62 与容器 DOVI
// conf), 解析成 HdrMeta / DoviMeta 后交给宿主回调。**与用哪条解码腿无关**: 硬解
// (D3D11VA / VideoToolbox / MediaCodec / Vulkan) 不解码流内的这些 NAL, FFmpeg 也
// 不会给硬解帧挂 DOVI/HDR side data, 所以必须由本类在喂包时旁路扫描。
//
// 用法: VideoDecoder 持有一个实例, 在 decode() 前调 extract(packet), 命中就派发
// onHdrMeta / onDoviMeta。
class MetaExtractor {
 public:
  // 元数据回调(由 VideoDecoder 转发到 IVideoDecoderOb)
  struct Callbacks {
    // HDR 静态元数据/亮度变化时回调(已按真值去重)
    std::function<void(const HdrMeta&)> onHdrMeta;
    // DV 整形数据(场景变化, 已 memcmp 去重)时回调
    std::function<void(const DoviMeta&)> onDoviMeta;
  };

  explicit MetaExtractor(Callbacks callbacks) : cb(std::move(callbacks)) {}
  ~MetaExtractor() = default;

  MetaExtractor(const MetaExtractor&) = delete;
  MetaExtractor& operator=(const MetaExtractor&) = delete;

  // 设置流参数: 决定扫哪几类 NAL、DV 上下文怎么起
  void setCodec(VCodecId codecId_) { codecId = codecId_; }
  // 容器侧 DOVI conf(avcc 判定同 VideoDecoder: 决定 NAL 拆分方式)
  void setDvProfile(int32_t profile) { dvProfile = profile; }
  void setAvcc(bool b) { bAvcc = b; }

  // 扫描一个待解码的包, 命中则回调
  void extract(const AvoxPacket& packet);

  // 会话边界(seek / 重开流)复位: 清 RPU 上下文与去重状态
  void reset();

 private:
  // NAL 拆分 + 逐 NAL 扫描(SEI / RPU)
  void scanNalus(const AvoxPacket& packet);
  // SEI RBSP → 137(mdcv)/144(clli)
  void parseSei(const uint8_t* rbsp, int32_t size, HdrMeta& meta);
  // RPU(RBSP) → DoviMeta / L1 亮度, 写入 hdrMeta/doviMeta
  void parseRpu(const uint8_t* rbsp, int32_t size, HdrMeta& hdr, DoviMeta& dovi);

  // 宿主回调(转发到 IVideoDecoderOb)
  Callbacks cb;
  VCodecId codecId = VCodecId::h265;
  int32_t dvProfile = 0;
  bool bAvcc = false;
  // 上次下发的值(去重闸)
  HdrMeta lastHdr = {};
  DoviMeta lastDovi = {};
  // 反仿真临时缓冲(RPU / SEI 载荷都很小, 复用以免反复分配)
  std::vector<uint8_t> rbspBuf;
  // 探针限次: RPU 每帧都有, 首几次打日志即可(防刷屏)
  int32_t rpuOkLogs = 0;
  int32_t rpuFailLogs = 0;
  // DV 解析上下文(ff_dovi_rpu_parse 的 DOVIContext), 不透明存放避免头污染
  void* doviCtx = nullptr;
};

}  // namespace avox
