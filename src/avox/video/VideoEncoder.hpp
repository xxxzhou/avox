#pragma once

#include "../AvoxMuxer.h"
#include "../module/Observer.hpp"
#include "../muxer/AVEncoder.hpp"
#include "ImageBuffer.hpp"
#include "VideoEncodeParams.hpp"
#include "VideoRender.hpp"

namespace avox {

class AVOX_EXPORT VideoEncoder : public AVEncoder, public Observer<IEncoderOb> {
 public:
  VideoEncoder();
  virtual ~VideoEncoder();

 protected:
  VTrackDesc desc = {};
  // 关键帧间隔(秒): 单一来源是 encodeParam.gopSeconds, 由 setEncodeParam 同步
  int32_t gop = 3;
  // 用户旋钮(逐项独立)。未设置的项由用途档推导 —— 见 getRateMode 等解析函数
  VideoEncodeParam encodeParam = {};
  // 用途档(内部; 决定未设置项的缺省取向): 录制 = record, 推流 = stream
  EncodeUsage usage = EncodeUsage::record;
  // 车道选中的编码器名(注册表名, 如 "h264_mf"): 上层选软/硬的意图在候选链里
  // 已表达, 实现据此取用对应编码器; 空 = 由实现自行挑选
  std::string encoderName = "";
  // setBitrate 是否被显式调过: 显式码率 = 外部硬约束(WebRTC 上行带宽)
  bool bExplicitBitrate = false;
  // AvoxSurfaceType surface = nullptr;

 protected:
  // 自动计算bitrate
  int32_t getAutoBitrate() const;

 public:
  void setDesc(const VTrackDesc& desc);
  void setBitrate(int32_t bitrate);
  // 录制侧编码参数下发(非虚: 只搬字段, 不动虚表)
  void setEncodeParam(const VideoEncodeParam& param);
  void setUsage(EncodeUsage usage_) { usage = usage_; }
  EncodeUsage getUsage() const { return usage; }
  void setEncoderName(const std::string& name) { encoderName = name; }
  const std::string& getEncoderName() const { return encoderName; }
  // 目标码率(bps): 显式设置优先, 否则按输入 desc 自动算
  int32_t getTargetBitrate() const {
    return bExplicitBitrate ? bitrate : getAutoBitrate();
  }
  int32_t getGopSeconds() const { return gop; }
  // 以下四项 = 已解析值(未指定项按用途档推导), 编码器实现只读这几个
  EncodeRateMode getRateMode() const;
  int32_t getQualityLevel() const;
  EncodePreset getPreset() const;
  EncodeProfile getProfileLevel() const;
  // virtual void onSurface() {};
  virtual DecodeResult encode(const YUVFrame& frame) {
    return DecodeResult::noSupport;
  };
  virtual DecodeResult encode(const GpuFrame& frame) {
    return DecodeResult::noSupport;
  };
  // 排空编码器内部缓冲、把尾帧吐完(停流时用)。与 flush 的区别: flush 是
  // "丢弃缓冲并复位"(seek 场景), drain 是"吐完再停" —— 质量定档的编码器前瞻
  // 缓冲可达几十帧, 拿 flush 顶替会静默吃掉尾部若干帧。
  // ⚠️ 新虚函数只能追加在类末尾(往中间插会移动既有槽位, 旧二进制静默调错)
  virtual void drain() {}
};

}