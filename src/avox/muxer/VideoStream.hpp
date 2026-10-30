#pragma once

#include <atomic>

#include "../codec/VideoEncoderChain.hpp"
#include "../video/VideoEncoder.hpp"

namespace avox {

// 打开线程,使用编码器把原始数据编码
class VideoStream : public IEncoderOb,
                    public IMuxerContext,
                    private VideoEncoderChain::Host {
public:
  VideoStream();
  virtual ~VideoStream();

protected:
  VTrackDesc desc = {};
  // 是否硬编
  bool bHardEncoder = true;
  // 编码参数(逐项独立旋钮: 码控模式/质量/码率/预设/profile/关键帧间隔);
  // 未设置项由编码器按用途档推导。车道接线时下发给选中的那个编码器
  VideoEncodeParam encodeParam = {};
  VideoEncoderChain encoderChain;
  // 本车道是否已出过包: 出包前换道安全(封装头/编码描述还没落), 出包后不再换
  bool bEmittedPacket = false;
  // 候选链试完仍打不开: 只报一次(否则逐帧刷屏)
  bool bWarnedEncDead = false;
  // 编码器拒收计数(send_frame EAGAIN=输入缓冲满, 帧被静默丢弃), open 前清零
  std::atomic<int64_t> encoderDropped{0};

  // 候选链宿主: 编码器的"打不开"在首帧(onPreEncoder)不在建实例, 故这里只接线,
  // laneInit 恒成功; 真正的换道由 encoderFrame 见 openFailed 时发起
  virtual bool laneInit(VideoEncoder &impl, const VCodecDesc &vdesc) override;
  virtual void laneClose(VideoEncoder &impl) override;
  virtual void laneSaveConfigs(VideoEncoder &oldImpl) override;
  virtual void laneChanged(VideoEncoder &newImpl, bool bFirst) override;
  VideoEncoder *encoder() { return encoderChain.impl(); }
  // 编码一帧; 首帧前打不开就换道重编(YUVFrame/GpuFrame 两个重载共用)
  template <typename TFrame>
  void encodeFrameImpl(const TFrame &frame);

public:
  void setHardEncode(bool bHard);
  bool getHardEncode();
  // 编码器拒收帧数(send EAGAIN); 高码率/低性能机上非零, 应提示降码率
  int64_t getEncoderDropped() const { return encoderDropped.load(); }
  void setEncodeParam(const VideoEncodeParam &param) { encodeParam = param; }
  void setVideoDesc(const VTrackDesc &desc);
  void encoderFrame(const YUVFrame &frame);
  void encoderFrame(const GpuFrame &frame);
  // flush 编码器(seek 时重置 GOP/参考帧,下一帧自然 IDR)
  void flushEncoder() { if (encoder()) encoder()->flush(); }
// 停编码器: 排空(待出帧在 sink 尚活时出完) → 析构会话 → 断 muxer 链。
// 关流必须「先停编码器再放 sink」: 硬编回调在 VT 队列上异步到达, sink 先死
  void stopEncoder();

  // IEncoderOb
public:
  virtual void onPacket(AvoxPacket &packet) override;
};

}
