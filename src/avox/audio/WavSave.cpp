#include "WavSave.hpp"
#include "../module/LogHelper.hpp"

namespace avox {

WavSave::~WavSave() { close(); }

bool WavSave::setAudioDesc(const AudioDesc& desc_) {
  if (!desc_.bValid()) {
    LOGFLF(LogLevel::warn, "WavSave invalid desc");
    return false;
  }
  // WAV 只支持 u8/s16/s32/flt/dbl 及其 planar 变体,不支持 s64/dblp
  if (desc_.format == AudioFormat::AVOX_AUDIO_S64 ||
      desc_.format == AudioFormat::AVOX_AUDIO_S64P) {
    LOGFLF(LogLevel::warn, "WavSave unsupported format:", getAudioFormatStr(desc_.format));
    return false;
  }
  desc = desc_;
  LOGFLF(LogLevel::info, "WavSave desc set, ch:", desc.channels,
         " rate:", desc.sampleRate, " fmt:", getAudioFormatStr(desc.format));
  return true;
}

bool WavSave::openUrl(const char* path) {
  close();
  if (!path) {
    LOGFLF(LogLevel::warn, "WavSave invalid path");
    return false;
  }
  file.open(path, std::ios::binary);
  if (!file.is_open()) {
    LOGFLF(LogLevel::warn, "WavSave open failed:", path);
    return false;
  }
  // 预留 44 字节 WAV 头,close 时回填
  file.seekp(44);
  bOpen = true;
  LOGFLF(LogLevel::info, "WavSave opened:", path);
  return true;
}

void WavSave::addFrame(const AvoxData& raw) {
  if (!bOpen || !desc.bValid() || !raw.data || raw.size <= 0) {
    return;
  }
  const uint8_t* writeData = raw.data;
  int32_t writeSize = raw.size;
  // planar 格式需转 interleaved
  if (bAPlaneFormat(desc.format)) {
    planeToInterleaved(raw.data, raw.size);
    writeData = pbuffer.data();
    writeSize = (int32_t)pbuffer.size();
  }
  file.write(reinterpret_cast<const char*>(writeData), writeSize);
  dataSize += writeSize;
}

void WavSave::close() {
  if (!bOpen) {
    return;
  }
  // 写入 WAV 头(此时 desc 一定已设)
  if (desc.bValid()) {
    file.seekp(0);
    writeWavHeader(dataSize);
  }
  file.close();
  LOGFLF(LogLevel::info, "WavSave closed, data bytes:", dataSize);
  bOpen = false;
  dataSize = 0;
}

void WavSave::planeToInterleaved(const uint8_t* data, int32_t size) {
  int32_t channels = desc.channels;
  int32_t bps = audioFormatSize(desc.format);
  // planar 布局: [ch0_samples][ch1_samples]...
  // 每声道采样数 = 总字节数 / (声道数 * 每采样字节数)
  int32_t samplesPerCh = size / (channels * bps);
  int32_t outSize = samplesPerCh * channels * bps;
  pbuffer.resize(outSize);
  uint8_t* dst = pbuffer.data();
  for (int32_t s = 0; s < samplesPerCh; ++s) {
    for (int32_t c = 0; c < channels; ++c) {
      const uint8_t* src = data + c * samplesPerCh * bps + s * bps;
      memcpy(dst, src, bps);
      dst += bps;
    }
  }
}

void WavSave::writeWavHeader(uint32_t dataBytes) {
  uint16_t channels = desc.channels;
  uint32_t sampleRate = desc.sampleRate;
  // planar 格式在 WAV 中按对应的 interleaved 格式写
  AudioFormat wavFmt = bAPlaneFormat(desc.format)
                           ? nPlaneFormat(desc.format)
                           : desc.format;
  uint16_t bitsPerSample = audioFormatSize(wavFmt) * 8;
  uint16_t blockAlign = channels * bitsPerSample / 8;
  uint32_t byteRate = sampleRate * blockAlign;
  // PCM=1, IEEE float=3
  uint16_t audioFormat = (wavFmt == AudioFormat::AVOX_AUDIO_FLT ||
                          wavFmt == AudioFormat::AVOX_AUDIO_DBL)
                             ? 3
                             : 1;
  uint8_t header[44] = {};
  // RIFF chunk
  memcpy(header, "RIFF", 4);
  uint32_t fileSize = 36 + dataBytes;
  memcpy(header + 4, &fileSize, 4);
  memcpy(header + 8, "WAVE", 4);
  // fmt sub-chunk
  memcpy(header + 12, "fmt ", 4);
  uint32_t fmtSize = 16;
  memcpy(header + 16, &fmtSize, 4);
  memcpy(header + 20, &audioFormat, 2);
  memcpy(header + 22, &channels, 2);
  memcpy(header + 24, &sampleRate, 4);
  memcpy(header + 28, &byteRate, 4);
  memcpy(header + 32, &blockAlign, 2);
  memcpy(header + 34, &bitsPerSample, 2);
  // data sub-chunk
  memcpy(header + 36, "data", 4);
  memcpy(header + 40, &dataBytes, 4);
  file.write(reinterpret_cast<const char*>(header), 44);
}

IWavSave* createWavSave() { return new WavSave(); }

}
