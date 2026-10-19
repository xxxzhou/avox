#pragma once

#include "FFEncoder.hpp"
#include "avox/AvoxTime.h"
#include "avox/video/VideoEncoder.hpp"

namespace avox {

class FFVEncoder : public FFEncoder, public VideoEncoder {
public:
  FFVEncoder();
  virtual ~FFVEncoder();

protected:
  bool bHwEncode = false;
  YuvType yuvType = YuvType::other;
  // 已送进编码器的帧数(排空日志用: 产物尾帧缺失时先看是"没喂够"还是"没吐完")
  int32_t sentFrames = 0;

public:
  virtual DecodeResult onPreEncoder() override;
  virtual DecodeResult encode(const YUVFrame &frame) override;
  // flush 重置编码器 GOP/参考帧,seek 后下一帧自然为 IDR
  virtual void flush() override;
  // 排空: send NULL 把内部缓冲(质量档前瞻)吐完, 见 VideoEncoder::drain
  virtual void drain() override;

protected:
  virtual void onAttachContext() {};
  // 挑编码器: 候选链选中名 → 渠道默认名 → (Windows)nv12 探测平台硬编 → 首个实现
  const AVCodec *selectCodec();
  // 建编码上下文; 每级重试都重建(上一级可能已把 ctx 改到半开状态)
  void fillContext(const AVCodec *codec, const FrameRate &framerate,
                   int32_t bitrate, int32_t tier);
  // 按编码器名分发档位参数(取向与实测依据见 .cpp 顶部与各分支注释)
  void applyEncoderParams(AVCodecContext *ctx, const AVCodec *codec,
                          int32_t bitrate, int32_t quality, int32_t tier,
                          AVDictionary **param);
};

}