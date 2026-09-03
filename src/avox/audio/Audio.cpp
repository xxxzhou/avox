#include "../AvoxAudio.h"
#include "../module/LogHelper.hpp"
#include "AudioRender.hpp"

namespace avox {

const int sampleRateTable[] = {96000, 88200, 64000, 48000, 44100, 32000, 24000,
                               22050, 16000, 12000, 11025, 8000,  7350};

int32_t getAudioFrameSize(const AudioDesc& audioDesc, int32_t frameMs) {
  int64_t samples = audioDesc.sampleRate;
  samples = samples * audioFormatSize(audioDesc.format);
  samples = samples * audioDesc.channels;
  int32_t msSize = frameMs * samples / 1000;
  return msSize;
}

int32_t getAudioFrameMs(const AudioDesc& audioDesc, int32_t size) {
  int64_t samples = audioDesc.sampleRate;
  samples = samples * audioFormatSize(audioDesc.format);
  samples = samples * audioDesc.channels;
  int32_t msSize = size * 1000 / samples;
  return msSize;
}

int32_t audioFormatSize(AudioFormat format) {
  switch (format) {
#define XX(name, value, size, panel, str) \
  case AudioFormat::name:                 \
    return size;
    AVOX_MAP_AUDIO_FMT(XX)
#undef XX
    default:
      return 0;
  }
}

int32_t getSamples(int32_t size, const AudioDesc& desc) {
  // 每个采样点多少字节
  int32_t block = desc.channels * audioFormatSize(desc.format);
  // 多少信息
  return size / block;
}

const char* getAudioFormatStr(AudioFormat format) {
  switch (format) {
#define XX(name, value, size, plane, str) \
  case AudioFormat::name:                 \
    return str;
    AVOX_MAP_AUDIO_FMT(XX)
#undef XX
    default:
      return "invalid";
  }
}

bool bAPlaneFormat(AudioFormat format) {
  switch (format) {
#define XX(name, value, size, plane, str) \
  case AudioFormat::name:                 \
    return plane;
    AVOX_MAP_AUDIO_FMT(XX)
#undef XX
    default:
      return false;
  }
}

AudioFormat nPlaneFormat(AudioFormat format) {
  switch (format) {
    case AudioFormat::AVOX_AUDIO_U8P:
      return AudioFormat::AVOX_AUDIO_U8;
    case AudioFormat::AVOX_AUDIO_S16P:
      return AudioFormat::AVOX_AUDIO_S16;
    case AudioFormat::AVOX_AUDIO_S32P:
      return AudioFormat::AVOX_AUDIO_S32;
    case AudioFormat::AVOX_AUDIO_S64P:
      return AudioFormat::AVOX_AUDIO_S64;
    case AudioFormat::AVOX_AUDIO_FLTP:
      return AudioFormat::AVOX_AUDIO_FLT;
    case AudioFormat::AVOX_AUDIO_DBLP:
      return AudioFormat::AVOX_AUDIO_DBL;
    default:
      return format;
  }
}

bool bAdtsHeader(const uint8_t* data, int32_t size) {
  if (size >= 7 && data[0] == 0xFF && (data[1] & 0xF0) == 0xF0) {
    return true;
  }
  return false;
}

struct AdtsHeader {
 public:
  // 12 bslbf 同步字The bit string ‘1111 1111 1111’，说明一个ADTS帧的开始
  unsigned int syncword = 0;
  // 1 bslbf   MPEG 标示符, 设置为1
  unsigned int id;
  // 2 uimsbf Indicates which layer is used. Set to ‘00’
  unsigned int layer;
  // 1 bslbf  表示是否误码校验
  unsigned int protection_absent;
  // 2 uimsbf  表示使用哪个级别的AAC，如01 Low
  // Complexity(LC)--- AACLC
  unsigned int profile;
  // 4 uimsbf  表示使用的采样率下标
  unsigned int sf_index;
  // 1 bslbf
  unsigned int private_bit;
  // 3 uimsbf  表示声道数
  unsigned int channel_configuration;
  // 1 bslbf
  unsigned int original;
  // 1 bslbf
  unsigned int home;
  // 下面的为改变的参数即每一帧都不同
  // 1 bslbf
  unsigned int copyright_identification_bit;
  // 1 bslbf
  unsigned int copyright_identification_start;
  // 13 bslbf  一个ADTS帧的长度包括ADTS头和raw data block
  unsigned int aac_frame_length;
  // 11 bslbf     0x7FF 说明是码率可变的码流
  unsigned int adts_buffer_fullness;
  // no_raw_data_blocks_in_frame
  // 表示ADTS帧中有number_of_raw_data_blocks_in_frame + 1个AAC原始帧.
  // 所以说number_of_raw_data_blocks_in_frame == 0
  // 表示说ADTS帧中有一个AAC数据块并不是说没有。(一个AAC原始帧包含一段时间内1024个采样及相关数据)
  // 2 uimsfb
  unsigned int no_raw_data_blocks_in_frame;
};

uint8_t getSampleReteIndex(int32_t sampleRate) {
  // 采样率索引表
  uint8_t sampleRateIndex = 12;
  for (int i = 0; i < sizeof(sampleRateTable) / sizeof(sampleRateTable[0]);
       ++i) {
    if (sampleRateTable[i] == sampleRate) {
      sampleRateIndex = i;
      break;
    }
  }
  if (sampleRateIndex == 12) {
    LOGFLF(LogLevel::warn, "sampleRateIndex not find");
  }
  return sampleRateIndex;
}

int32_t getSampleRateByIndex(uint8_t index) {
  if (index < sizeof(sampleRateTable) / sizeof(sampleRateTable[0])) {
    return sampleRateTable[index];
  }
  return 16000;
}

void adtsHeader(uint8_t* data, int32_t size, uint8_t sampleRateIndex,
                int32_t channelCount, int32_t profile) {
  if (!data) {
    return;
  }
  // frame length = payload + adts header length (7 when protection_absent=1)
  const int protectionAbsent = 1;  // 如果改为0，header长度为9
  const int headerLen = protectionAbsent ? 7 : 9;
  int aac_frame_length = size + headerLen;

  // 清零，避免残留位
  memset(data, 0, 7);

  // profile 字段是 audioObjectType - 1（例如 AAC-LC audioObjectType=2 ->
  // profile_field=1）
  uint8_t profile_field = 0;
  if (profile > 0)
    profile_field = (uint8_t)((profile - 1) & 0x03);
  else
    profile_field = 0;

  uint8_t sf_index = sampleRateIndex & 0x0F;
  uint8_t chan_cfg = (uint8_t)(channelCount & 0x07);  // 3 bits

  // syncword 12 bits 0xFFF
  data[0] = 0xFF;
  // 4 high bits of second byte set to 0xF (synclow), then ID, layer,
  // protection_absent
  data[1] = 0xF0;
  data[1] |= (0 /* MPEG-4: ID=0 */ << 3);
  data[1] |= (0 /* layer */ << 1);
  data[1] |= (protectionAbsent & 0x01);

  // profile(2) | sf_index(4) | private_bit(1) | channel_config high bit
  data[2] = (uint8_t)((profile_field & 0x03) << 6);
  data[2] |= (uint8_t)((sf_index & 0x0F) << 2);
  data[2] |= (uint8_t)(0 << 1);  // private bit = 0
  data[2] |= (uint8_t)((chan_cfg & 0x04) >> 2);

  // channel_config低2bits | original | home | copyright... | aac_frame_length
  // high2bits
  data[3] = (uint8_t)((chan_cfg & 0x03) << 6);
  data[3] |= (uint8_t)(0 << 5);  // original
  data[3] |= (uint8_t)(0 << 4);  // home
  data[3] |= (uint8_t)(0 << 3);  // copyright id bit
  data[3] |= (uint8_t)(0 << 2);  // copyright id start
  data[3] |= (uint8_t)((aac_frame_length & 0x1800) >> 11);

  // frame length 中间 8 bits
  data[4] = (uint8_t)((aac_frame_length & 0x7F8) >> 3);

  // frame length 低3bits | adts_buffer_fullness high5bits
  data[5] = (uint8_t)((aac_frame_length & 0x7) << 5);
  // 默认 adts_buffer_fullness = 0x7FF (VBR)
  data[5] |= (uint8_t)((0x7FF >> 6) & 0x1F);

  // adts_buffer_fullness 低6bits | number_of_raw_data_blocks_in_frame (2bits)
  data[6] = (uint8_t)((0x7FF & 0x3F) << 2);
}

void addAdtsHeader(const AudioDesc& audioDesc, uint8_t* data, int32_t size,
                   int32_t profile) {
  uint8_t sampleRateIndex = getSampleReteIndex(audioDesc.sampleRate);
  adtsHeader(data, size, sampleRateIndex, audioDesc.channels, profile);
}

void ascHeader(const AudioDesc& audioDesc, uint8_t* data, int32_t profile) {
  // 音频对象类型 (AAC Profile)
  uint8_t audioObjectType = profile;
  // 采样率索引
  uint8_t sampleRateIndex = getSampleReteIndex(audioDesc.sampleRate);
  // 声道配置
  uint8_t channelConfig = audioDesc.channels;
  // 构建16位配置值
  uint16_t config = 0;
  // 5 bits audioObjectType
  config |= (audioObjectType & 0x1F) << 11;
  // 4 bits sampleRateIndex
  config |= (sampleRateIndex & 0x0F) << 7;
  // 4 bits channelConfiguration
  config |= (channelConfig & 0x0F) << 3;
  // 写入2字节
  data[0] = (config >> 8) & 0xFF;
  data[1] = config & 0xFF;
}

}