#pragma once

#include <fstream>
#include <vector>

#include "../AvoxAudio.h"

namespace avox {

// WAV 文件保存实现:将 PCM 音频数据写入可播放的 WAV 文件
// planar 格式自动转 interleaved; WAV 头先占位,close 时回填文件大小
class WavSave : public IWavSave {
 public:
  WavSave() = default;
  ~WavSave() override;

 public:
  bool setAudioDesc(const AudioDesc& desc) override;
  bool openUrl(const char* path) override;
  void addFrame(const AvoxData& raw) override;
  void close() override;

 private:
  // planar → interleaved 转换,结果写入 pbuffer
  void planeToInterleaved(const uint8_t* data, int32_t size);
  // 写入 44 字节 WAV 头
  void writeWavHeader(uint32_t dataBytes);

  std::ofstream file;
  AudioDesc desc = {};
  uint32_t dataSize = 0;
  // planar→interleaved 临时缓冲
  std::vector<uint8_t> pbuffer;
  bool bOpen = false;
};

}
