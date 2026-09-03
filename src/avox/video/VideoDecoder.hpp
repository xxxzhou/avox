#pragma once
#include "../AvoxCodec.h"
#include "../codec/H264Parse.hpp"
#include "../codec/H265Parse.hpp"
#include "../module/Observer.hpp"
#include "../module/RunTask.hpp"
#include "../player/AVDecoder.hpp"
#include "../player/Player.hpp"
#include "../source/PacketBuf.hpp"
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
  DecoderParams params = {};
  VCodecTh codecTH = VCodecTh::cpu;
  // 是否有B帧，有些解码器需要自己处理B帧
  bool bHaveBFrame = false;
  // 检查是否annexb/avcc
  bool bCheckAcc = false;
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
