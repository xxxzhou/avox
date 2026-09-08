#include "RtcAudioDecoder.hpp"

#include "avox/module/AvoxManager.hpp"
#include "modules/audio_coding/codecs/legacy_encoded_audio_frame.h"

namespace avox {

using namespace webrtc;

RtcAudioDecoder::RtcAudioDecoder(const AudioDesc& adesc_) {
  adesc = adesc_;
  findDecoder();
}

void RtcAudioDecoder::findDecoder() {
  bool bFind = AvoxManager::Get().aDecoders.hasObjectId(ACodecId::aac);
  if (!bFind) {
    log(LogLevel::info, "not find aac decoder");
    return;
  }
  const auto& decodes = AvoxManager::Get().aDecoders.initFuncs(ACodecId::aac);
  if (decodes.size() < 0) {
    return;
  }
  // 优先使用fdk-aac解码器
  size_t sIndex = 0;
  // fdk-aac decoder/ffmpeg_aac
  const char* sName = "fdk-aac decoder";
  for (size_t i = 0; i < decodes.size(); ++i) {
    if (decodes[i].desc.name == sName) {
      sIndex = i;
      break;
    }
  }
  // 现在就用第一个来初始化解码器
  auto& aDecode = decodes[sIndex];
  decode = std::unique_ptr<avox::AudioDecoder>(aDecode.initFunc());
  if (!decode) {
    return;
  }
  decode->setObserver(this);
  decode->setWebrtc(true);
  bool bInit = decode->setContext(aDecode.desc, adesc);
  if (!bInit) {
    log(LogLevel::info, "aac decoder not support");
    return;
  }
  AvoxPacket pkt = {};
  std::vector<uint8_t> asc(2);
  ascHeader(adesc, asc.data());
  pkt.data = {asc.data(), static_cast<int>(asc.size()), true};
  pkt.packtype = (int32_t)PackType::aconfig;
  pkt.prefixSize = 2;
  decode->pushConfig(pkt);
#if AVOX_TEST_AUDIO_RECORDER
  bRecordPcm = true;
  if (bRecordPcm) {
    fileStream.open(filePath, std::ios::binary | std::ios::out);
  }
#endif
}

RtcAudioDecoder::~RtcAudioDecoder() {
  if (decode) {
    decode->close();
    decode->removeObserver(this);
    decode.reset();
  }
#if AVOX_TEST_AUDIO_RECORDER
  if (fileStream.is_open()) {
    fileStream.close();
  }
#endif
}

void RtcAudioDecoder::Reset() { findDecoder(); }

int RtcAudioDecoder::SampleRateHz() const {
  // log(LogLevel::info, "sampleRateHz:", adesc.sampleRate);
  return adesc.sampleRate;
}

size_t RtcAudioDecoder::Channels() const {
  // log(LogLevel::info, "channels:", adesc.channels);
  return adesc.channels;
}

int RtcAudioDecoder::PacketDuration(const uint8_t* encoded,
                                    size_t encoded_len) const {
  // 返回的是单通道后解码采样时长*每毫秒采样数，单位时间内的采样数
  return 1024;
}

int RtcAudioDecoder::DecodeInternal(const uint8_t* encoded, size_t encoded_len,
                                    int sample_rate_hz, int16_t* decoded,
                                    SpeechType* speech_type) {
  if (!decode) {
    return -1;
  }
  AvoxPacket pkt = {};
  pkt.data = {const_cast<uint8_t*>(encoded), static_cast<int>(encoded_len),
              true};
  DecodeResult result = decode->decode(pkt);
  if (result != DecodeResult::success) {
    return 0;
  }
  *speech_type = AudioDecoder::SpeechType::kSpeech;
  // onDecod与这是同一线程
  // decoded = (int16_t *)frame.buffer.data;
  memcpy(decoded, frame.buffer.data, frame.buffer.size);
  // log(LogLevel::info, "aac data buffer size:", frame.buffer.size / 2);
#if AVOX_TEST_AUDIO_RECORDER
  if (fileStream.is_open()) {
    fileStream.write((char*)decoded, frame.buffer.size);
  }
#endif
  // webrtc以uint16_t计算长度，后面需要再除以2
  return frame.buffer.size / 2;
}

void RtcAudioDecoder::onDecode(const AvoxAFrame& frame_) { frame = frame_; }

// 这里现测试有点奇怪，payload有时不足一个包，需要多个palyload才能合成一个
std::vector<webrtc::AudioDecoder::ParseResult> RtcAudioDecoder::ParsePayload(
    webrtc::Buffer&& payload, uint32_t timestamp) {
  std::vector<AudioDecoder::ParseResult> results;
  // uint32_t last_dts = 0;
  // rtp数据开始部分
  auto ptr = payload.data();
  size_t payloadSize = payload.size();
  // rtp数据末尾
  auto end = ptr + payloadSize;
  // AU-headers用多少字节表示长度，一般是16bit二个字节
  auto au_header_size = ((ptr[0] << 8) | ptr[1]) / 8;
  // 记录au_header起始指针
  auto au_header_ptr = ptr + au_header_size;
  // 从au_header读取长度
  uint16_t data_size = ((au_header_ptr[0] << 8) | au_header_ptr[1]) >> 3;
  int32_t payDatasize = payloadSize - au_header_size - 2;
  // AAC数据起始位置
  ptr = au_header_ptr + 2;
  if (end < ptr) {
    // 数据不够
    return results;
  }
  // 如果包没拆，其data_size = payDatasize
  if (data_size == payDatasize) {
    // 设置aac数据
    webrtc::Buffer new_payload((uint8_t*)ptr, data_size);
    std::unique_ptr<LegacyEncodedAudioFrame> frame(
        new LegacyEncodedAudioFrame(this, std::move(new_payload)));
    results.emplace_back(timestamp, 0, std::move(frame));
  } else if (data_size > payDatasize) {
    // 添加到包
    rtpData.insert(rtpData.end(), ptr, ptr + payDatasize);
    if (rtpData.size() >= data_size) {
      // 正常应该相等
      if (rtpData.size() > data_size) {
        log(LogLevel::warn, "rtp data size:", rtpData.size(),
            " aac data size:", data_size, " timestamp:", timestamp,
            " preRtpTime:", preRtpTime, " au_header_size:", au_header_size);
      }
      webrtc::Buffer new_payload((uint8_t*)rtpData.data(), rtpData.size());
      std::unique_ptr<LegacyEncodedAudioFrame> frame(
          new LegacyEncodedAudioFrame(this, std::move(new_payload)));
      results.emplace_back(timestamp, 0, std::move(frame));
      rtpData.clear();
    }
  }
  if (!results.empty()) {
    // log(LogLevel::warn, "pay data size:", payDatasize,
    //     " aac data size:", data_size);
  }
  preRtpTime = timestamp;
  return results;
}

}
