#pragma once

#include <atomic>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

#include "../AvoxAudio.h"

namespace avox {

// WAV 文件保存实现:将 PCM 音频数据写入可播放的 WAV 文件。
// planar 格式自动转 interleaved; 头在 open 时占位(dataSize=0, 崩溃也是可播
// 文件), close/patchHeader 回填长度; 读写有锁, 采集线程与宿主线程可并发
class WavSave : public IWavSave {
 public:
  WavSave() = default;
  ~WavSave() override;

 public:
  bool setAudioDesc(const AudioDesc& desc) override;
  bool openUrl(const char* path) override;
  void addFrame(const AvoxData& raw) override;
  void close() override;
  int64_t patchHeader() override;
  int64_t getDataBytes() const override { return dataSize.load(); }
  bool bOpen() const override { return file.is_open(); }

 private:
  // planar → interleaved 转换,结果写入 pbuffer
  void planeToInterleaved(const uint8_t* data, int32_t size);
  // 写 44 字节头(dataBytes 为当前长度; open 占位与 patch 共用)
  bool writeWavHeader(uint32_t dataBytes);

  mutable std::mutex mtx;
  std::ofstream file;
  std::string path;
  AudioDesc desc = {};
  std::atomic<int64_t> dataSize{0};
  // planar→interleaved 临时缓冲(仅 addFrame 线程使用)
  std::vector<uint8_t> pbuffer;
  // 写失败/超上限只报一次(磁盘满逐帧刷屏无意义)
  bool bWarnedWrite = false;
};

}
