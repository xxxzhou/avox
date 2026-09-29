#pragma once
#include "../AvoxCodec.h"
#include "../codec/H264Parse.hpp"
#include "../codec/H265Parse.hpp"
#include "../module/Observer.hpp"
#include "../module/RunTask.hpp"
#include "../player/AVDecoder.hpp"
#include "../player/Player.hpp"
#include "../source/PacketBuf.hpp"
#include "MetaExtractor.hpp"
#include "VideoFrame.hpp"

namespace avox {

// 视频解码器
// 根据配置帧初始化
// 输入包数据,回调输出帧数据
class AVOX_EXPORT VideoDecoder : public AVDecoder, public Observer<IVideoDecoderOb> {
 public:
  VideoDecoder();
  virtual ~VideoDecoder() {};

 protected:
  // 当解码器没配置时,先缓存配置数据
  std::vector<PacketBuf> configPackets;
  // 视频元信息
  VideoDesc srcDesc = {};
  // 编码器元信息
  VCodecDesc codecDesc = {};
  VCodecId codecId = VCodecId::h264;
  // 添加针对配置帧的解析,有些解码器需要长宽等信息
  std::unique_ptr<H264Parse> h264Parse = nullptr;
  std::unique_ptr<H265Parse> h265Parse = nullptr;
  // 解码器无关的 HDR/DV 元数据提取(硬解腿不走 ffmpeg 的 side data, 须自扫码流)
  std::unique_ptr<MetaExtractor> metaExtractor = nullptr;
  // 容器 DOVI conf 的 profile(0=非DV)
  int32_t dvProfile = 0;
  DecoderParams params = {};
  VCodecTh codecTH = VCodecTh::cpu;
  // 是否有B帧，有些解码器需要自己处理B帧
  bool bHaveBFrame = false;
  // 检查是否annexb/avcc
  bool bCheckAcc = false;
  // 配置帧内容刚变化(add/update/updateSize)。pushConfig 会就地覆盖
  // configPackets,导致解码器侧"内容是否已存过"的判断恒为真;此标志把
  // "变化过"这件事独立记下来,供子类决定是否必须把该参数集喂进解码器
  bool bConfigChanged = false;
  // 是否是avcc/hvcc包
  bool bvcc = false;
  // MAC原生编码需要avcc/hvcc格式
  bool bMustVcc = false;
  // android原生编码需要annexb格式
  bool bMustAnnexb = false;
  // 分拆包
  std::vector<AvoxPacket> spiltBufs;
  // annexb3转码成annexb4
  std::vector<uint8_t> annexbBufs;

 public:
  const DecoderParams& getDecoderParams() { return params; }
  VCodecTh getCodecTh() { return codecTH; }

 public:
  // 容器 DOVI conf(0=非DV): 建解码器前由 VDecoderTask 从 VideoTrack 透传
  void setDvProfile(int32_t profile);
  bool setContext(const VCodecDesc& codecDesc, const VideoDesc& srcDesc);
  ConfigAddType pushConfig(const AvoxPacket& data);
  ConfigAddType pushConfig(const PacketBuf& data);
  // 有些解码器需要长宽等信息
  bool parseConfigs();
  // 复制当前配置帧
  void copyConfigs(std::vector<PacketBuf>& configs);
  DecodeResult decoder(avox::PacketBufPtr packet);
  // 原始包,可能需要拆分与转换,如annexb与avcc根据编码器要求转换
  DecodeResult decoderImp(AvoxPacket& packet);
};

}
