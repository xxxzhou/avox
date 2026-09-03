#pragma once

#include "../AvoxAudio.h"
#include "../AvoxBuffer.h"
#include "../AvoxCodec.h"
#include "../module/Observer.hpp"
#include "../module/RunTask.hpp"
#include "../player/AVDecoder.hpp"
#include "../player/Player.hpp"

namespace avox {

struct AacSC {
  int32_t confSampleIndex = 8;
  int32_t confChannel = 1;
  int32_t objectType = 2;
};

// 音频编码器,需要音频信息,然后传入包数据,回调中输出帧数据
class AVOX_EXPORT AudioDecoder : public AVDecoder, public Observer<IAudioDecoderOb> {
 public:
  AudioDecoder() = default;
  virtual ~AudioDecoder() {};

 protected:
  // 解码器可能改变输入格式
  AudioDesc outDesc = {};
  // 编码器元信息
  ACodecDesc codecDesc = {};
  // ACodecId codecId = ACodecId::aac;
  PacketBufPtr confPkt = nullptr;
  // 是否用在webrtc中,需要特殊处理
  bool bWebRtc = false;
  // 是否ADTS头，直播流一般是ADTS头
  bool bAdts = false;

 public:
  bool setContext(const ACodecDesc& codecDesc, const AudioDesc& srcDesc);
  void setWebrtc(bool value) { bWebRtc = value; }
  ConfigAddType pushConfig(const AvoxPacket& data);
  ConfigAddType pushConfig(const PacketBuf& data);
  DecodeResult decoder(avox::PacketBufPtr packet);

 public:
  AudioDesc getOutDesc() { return outDesc; }
};


void splitAAConfig(const PacketBuf& packet, AacSC& aacsc, bool& bAdts);

}
