#include "AudioDecoder.hpp"

#include "../player/AudioTrack.hpp"

namespace avox {

bool AudioDecoder::setContext(const ACodecDesc& acodecDesc,
                              const AudioDesc& srcDesc) {
  codecDesc = acodecDesc;
  // 默认和Track的输入类型一致
  outDesc = srcDesc;
  // codecId = codecDesc.codecId;
  return onVaild();
}

ConfigAddType AudioDecoder::pushConfig(const AvoxPacket& data) {
  PacketBuf buf(data);
  return pushConfig(buf);
}

ConfigAddType AudioDecoder::pushConfig(const PacketBuf& data) {
  confPkt = std::make_shared<PacketBuf>(data);
  return ConfigAddType::add;
}

DecodeResult AudioDecoder::decoder(avox::PacketBufPtr packet) {
  AvoxPacket aPacket = {};
  aPacket.data = {packet->buff.data(), packet->size, true};
  aPacket.pts = packet->pts;
  aPacket.dts = packet->dts;
  aPacket.prefixSize = packet->prefixSize;
  aPacket.frameType = packet->frameType;
  return decode(aPacket);
}

void splitAAConfig(const PacketBuf& packet, AacSC& aacsc, bool& bAdts) {
  AvoxData temp = {(uint8_t*)packet.buff.data(), std::min(packet.size, 10),
                  true};
  uint8_t* cfg = temp.data;
  if (bAdtsHeader(temp.data, temp.size)) {
    bAdts = true;
    // 1. 解析 objectType (Profile)
    aacsc.objectType = ((cfg[2] >> 6) & 0x03) + 1;
    // 2. 解析 sampleRate (采样率索引)
    aacsc.confSampleIndex = (cfg[2] >> 2) & 0x0F;
    // 3. 解析 channel (声道配置)
    aacsc.confChannel = ((cfg[2] & 0x01) << 2) | ((cfg[3] >> 6) & 0x03);
  } else {
    bAdts = false;
    aacsc.objectType = cfg[0] >> 3;
    aacsc.confSampleIndex = ((cfg[0] & 0x07) << 1) | (cfg[1] >> 7);
    aacsc.confChannel = (cfg[1] & 0x7F) >> 3;
  }
  std::string adtsStr = bAdts ? "adts" : "asc";
  LOGFLF(LogLevel::info, adtsStr, " objectType:", aacsc.objectType,
         " sampleRateIndex:", aacsc.confSampleIndex,
         " channel:", aacsc.confChannel, " data:", temp);
}

}
