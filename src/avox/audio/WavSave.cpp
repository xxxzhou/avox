#include "WavSave.hpp"
#include "../module/LogHelper.hpp"
#include "../subtitle/CharsetConvert.hpp"

#include <algorithm>

namespace avox {

namespace {
// 单轨 4GB 上限(32 位长度字段); 48k 立体声 s16 约 6 小时, float32 约 3 小时
constexpr int64_t kWavDataLimit = 0xFFFFFFF0LL;
}  // namespace

WavSave::~WavSave() { close(); }

bool WavSave::setAudioDesc(const AudioDesc& desc_) {
  std::lock_guard<std::mutex> lock(mtx);
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

bool WavSave::openUrl(const char* path_) {
  std::lock_guard<std::mutex> lock(mtx);
  if (file.is_open()) {
    file.close();
    dataSize = 0;
  }
  if (!path_ || !desc.bValid()) {
    LOGFLF(LogLevel::warn, "WavSave open without desc:", path_ ? path_ : "");
    return false;
  }
  // UTF-8 路径自愈(中文输出目录): MSVC 的 ofstream 直收宽路径, 其余平台窄路径
#if defined(_WIN32)
  file.open(utf8ToWide(path_).c_str(), std::ios::binary);
#else
  file.open(path_, std::ios::binary);
#endif
  if (!file.is_open()) {
    LOGFLF(LogLevel::warn, "WavSave open failed:", path_);
    return false;
  }
  path = path_;
  dataSize = 0;
  bWarnedWrite = false;
  // 头先落盘(长度占位 0): 进程被杀时文件仍是可播的空轨, 已写数据靠 patch 抢救
  if (!writeWavHeader(0)) {
    file.close();
    return false;
  }
  file.flush();
  LOGFLF(LogLevel::info, "WavSave opened:", path);
  return true;
}

void WavSave::addFrame(const AvoxData& raw) {
  if (!raw.data || raw.size <= 0) {
    return;
  }
  std::lock_guard<std::mutex> lock(mtx);
  if (!file.is_open() || !desc.bValid()) {
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
  if (dataSize + writeSize > kWavDataLimit) {
    if (!bWarnedWrite) {
      bWarnedWrite = true;
      LOGFLF(LogLevel::error, "wav size over 4GB, stop writing:", path);
    }
    return;
  }
  file.write(reinterpret_cast<const char*>(writeData), writeSize);
  if (!file.good()) {
    if (!bWarnedWrite) {
      bWarnedWrite = true;
      LOGFLF(LogLevel::error, "WavSave write failed:", path);
    }
    return;
  }
  dataSize += writeSize;
}

int64_t WavSave::patchHeader() {
  std::lock_guard<std::mutex> lock(mtx);
  if (!file.is_open()) {
    return dataSize.load();
  }
  const int64_t size = std::min<int64_t>(dataSize.load(), kWavDataLimit);
  writeWavHeader((uint32_t)size);
  // 回填头把写指针留在 44, 不恢复到数据末尾, 下一帧会从头部覆盖已写音频
  file.seekp(44 + size);
  file.flush();
  return dataSize.load();
}

void WavSave::close() {
  std::lock_guard<std::mutex> lock(mtx);
  if (!file.is_open()) {
    return;
  }
  writeWavHeader((uint32_t)std::min<int64_t>(dataSize.load(), kWavDataLimit));
  file.close();
  LOGFLF(LogLevel::info, "WavSave closed, data bytes:", dataSize.load(),
         " path:", path);
  // 不清 dataSize: 关文件后 session.json/宿主仍要读最终字节数; openUrl 时归零
}

bool WavSave::writeWavHeader(uint32_t dataBytes) {
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
  file.seekp(0);
  file.write("RIFF", 4);
  uint32_t fileSize = 36 + dataBytes;
  file.write((const char*)&fileSize, 4);
  file.write("WAVE", 4);
  file.write("fmt ", 4);
  uint32_t fmtSize = 16;
  file.write((const char*)&fmtSize, 4);
  file.write((const char*)&audioFormat, 2);
  file.write((const char*)&channels, 2);
  file.write((const char*)&sampleRate, 4);
  file.write((const char*)&byteRate, 4);
  file.write((const char*)&blockAlign, 2);
  file.write((const char*)&bitsPerSample, 2);
  file.write("data", 4);
  file.write((const char*)&dataBytes, 4);
  return file.good();
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

IWavSave* createWavSave() { return new WavSave(); }

}
