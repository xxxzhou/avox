#pragma once

#ifdef AVOX_ENABLE_FAAD2

#include <functional>

#include "AacHelper.hpp"
#include "avox/audio/AudioDecoder.hpp"
#include "avox/module/AvoxManager.hpp"

namespace avox {

class FaadDecoder : public AudioDecoder {
public:
  FaadDecoder();
  virtual ~FaadDecoder();

private:
  NeAACDecHandle handle = nullptr;
  NeAACDecConfigurationPtr config = nullptr;
  int32_t scrChannels = 1;
  int32_t confSampleIndex = 8;
  int32_t confChannel = 1;
  int32_t objectType = 2;
  // adts
  std::vector<uint8_t> aacData;
  bool bAudioDescNotified = false; 
  // AVDecoder
public:
  // 初始化
  virtual bool onVaild() override;
  // 初始化
  virtual DecodeResult onPreDecoder() override;
  // 解码
  virtual DecodeResult decode(const AvoxPacket &packet) override;
  // 解码完成
  virtual void flush() override;
  virtual void onClose() override;
};
}
#endif
