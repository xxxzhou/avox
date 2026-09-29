#pragma once

#include <atomic>
#include <memory>

#include "avox/module/RunTask.hpp"
#include "avox/source/AVSource.hpp"
// FFmpeg 辅助层(已导出): AVFormatContextPtr/AVPacketPtr/getUniquePtr/
// ffAvoxPacket/ffIoError/ff*Codec 映射, 与 IOParseFF 完全同源
#include "avox_ffmpeg/FFHelper.hpp"

struct bluray;

namespace avox {

// 蓝光原盘数据源: libbluray 选标题直读 + FFmpeg 自定义avio(mpegts)解封装。
// 管线与 IOParseTorrent 同构(AVSource 基类 processPacket 等全复用), 差异仅在
// 输入侧: read/seek 回调对接 bd_read/bd_seek, 解封装走 mpegts(m2ts)。
class IOParseDisc : public AVSource, public RunTask {
 public:
  IOParseDisc();
  virtual ~IOParseDisc();

 protected:
  // 流上下文(fmtCtx 的 CUSTOM_IO 下 pb 不随 close 释放, 所有权在本类)
  struct bluray* bd = nullptr;
  AVFormatContextPtr fmtCtx = nullptr;
  unsigned char* ioBuffer = nullptr;
  AVIOContext* ioCtx = nullptr;
  int64_t ioPosition = 0;
  AudioDesc audioDesc = {};
  bool bAACExtradata = false;
  std::atomic<bool> bInterruptRead{false};
  std::atomic<bool> bIoPausedAck{false};
  // BD PTS 基偏: m2ts 首包 dts 常带 ~600.5s 绝对基(tsMuxeR 合规起点), 不扣
  // 会让 position/进度条/seek 目标全飞——派发包统一扣基转相对, seek 反向加回
  int64_t ptsBaseMs = -1;

 private:
  // 解析轨道并派发配置包(SPS/PPS/VPS/ASC)
  bool parseStream(int32_t streamId, AVCodecParameters* codecpar);
  void parseH26xConfig(int32_t streamId, const uint8_t* extradata,
                       int32_t size, AVCodecID codecId);
  void parseAACConfig(int32_t streamId, const uint8_t* extradata,
                      int32_t size);
  // open 前的 option 快照(disc.* 键读取)
  void applyDiscOptions();
  void releaseIoContext();
  // 关闭并置空 bd 句柄(须已停读线程)
  void closeBd();

 protected:
  virtual void onRunTask() override;

 public:
  virtual bool onOpen() override;
  virtual void onClose() override;
  virtual void preSeek() override;
  virtual void pause(bool bFlag) override;
  virtual SeekType seekType() const override;
  virtual void seekTo(double progress) override;
  virtual bool seekTo(int64_t pos) override;
  virtual int64_t duration() const override;
  virtual double progress() const override;
  virtual bool vaild() override { return running(); }
  virtual void onOptionChange(const char* key, ArgType option) override;

 private:
  static int ioReadPacket(void* opaque, uint8_t* buf, int bufSize);
  static int64_t ioSeek(void* opaque, int64_t offset, int whence);
  static int decodeInterruptCb(void* ctx);
  bool interruptIo() const { return bInterruptRead.load(); }
  // 选中的标题时长(字节), AVSEEK_SIZE/SEEK_END 用
  uint64_t titleSize() const;
  int32_t discTitle = -1;  // -1 = 自动选最长标题
};

}
