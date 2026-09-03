#pragma once

#ifdef AVOX_ENABLE_FDKAAC

#include "aacdecoder_lib.h"
#include "avox/audio/AudioDecoder.hpp"
#include "avox/module/AvoxManager.hpp"

namespace avox {

class FdkaacDecoder : public AudioDecoder {
 public:
  FdkaacDecoder();
  virtual ~FdkaacDecoder();

 private:
  HANDLE_AACDECODER handle = nullptr;
  AAC_DECODER_ERROR lastError = AAC_DEC_OK;
  // 是否已通知 onAudioDesc
  bool bAudioDescNotified = false;
  int32_t confSampleIndex = 8;
  int32_t confChannel = 1;
  int32_t objectType = 2;
  // 解码缓冲区
  std::vector<uint8_t> decodeBuffer;
  std::vector<uint8_t> aacData;
  // 配置信息
  int32_t sampleRate = 0;
  int32_t channels = 0;
  int32_t frameSize = 0;

 public:
  // 初始化
  virtual bool onVaild() override;
  // 解码器预初始化
  virtual DecodeResult onPreDecoder() override;
  // 解码数据包
  virtual DecodeResult decode(const AvoxPacket& packet) override;
  // 刷新解码器
  virtual void flush() override;
  // 关闭解码器
  virtual void onClose() override;

 private:
  // 处理解码错误
  void handleDecodeError(AAC_DECODER_ERROR error);
  // 获取错误描述
  const char* getErrorDescription(AAC_DECODER_ERROR error);
};

}
#endif