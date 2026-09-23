#pragma once

#include <vector>

#include "avox/audio/IAudioTempo.hpp"
#include "soundtouch/SoundTouch.h"

namespace avox {

// IAudioTempo 的 SoundTouch(WSOLA) 实现: packed PCM → float 进 → 变速 → 转回原格式出
class SoundTouchTempo : public IAudioTempo {
 public:
  SoundTouchTempo() = default;
  ~SoundTouchTempo() override = default;

 private:
  bool init(const AudioDesc& desc) override;
  void setTempo(double speed) override;
  int process(const AvoxData& in) override;
  int receive(AvoxData& out) override;
  void reset() override;
  int32_t latencyMs() override;

 private:
  // 喂入转 float / 输出转回原格式, outBuf 按 receive 切片游标消费
  AudioDesc desc = {};
  int32_t frameBytes = 0;  // 一样本帧字节数(channels×fmtSize)
  int32_t sliceBytes = 0;  // receive 单次给设备的切片口径(≈40ms)
  soundtouch::SoundTouch processor;
  std::vector<float> feedBuf;
  std::vector<float> pullBuf;
  std::vector<uint8_t> outBuf;
  size_t outOffset = 0;
};

}
