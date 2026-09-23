#pragma once

#include "../AvoxAudio.h"

namespace avox {

// 音频变速不变调处理器: 解码后 packed PCM 进 → 同格式 PCM 出(总量≈输入/tempo)。
// 实现由插件提供(avox_tempo loadModule 时 reg "soundtouch"), 未装插件时调用方降级。
// 注意: 输出块边界与输入帧不对齐(内部攒窗); receive 返回的缓冲仅在下次
// process/receive/reset 前有效。
class IAudioTempo {
 public:
  virtual ~IAudioTempo() = default;

 public:
  // 输入 PCM 描述(packed interleaved; planar/不支持格式返回 false)
  virtual bool init(const AudioDesc& desc) = 0;
  // 变速系数(1.0 常速, 有效域约 0.25~4; 在线切换平滑无需冲刷)
  virtual void setTempo(double speed) = 0;
  // 喂入一段 PCM, 返回实际消费字节数(<=0 失败)
  virtual int process(const AvoxData& in) = 0;
  // 拉取输出块(可循环调用至返回 0), 返回字节数; out 指向内部缓冲
  virtual int receive(AvoxData& out) = 0;
  // 丢弃内部缓冲(seek 冲刷/回常速时调)
  virtual void reset() = 0;
  // 算法固有延迟 ms(已喂入未吐出的窗口量级, 无精确值时返回 0)
  virtual int32_t latencyMs() = 0;
};

}
