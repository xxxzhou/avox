#pragma once

#include <atomic>
#include <memory>

#include "TorrentEngine.hpp"
#include "avox/module/RunTask.hpp"
#include "avox/source/AVSource.hpp"
// FFmpeg 辅助层(已导出): AVFormatContextPtr/AVPacketPtr/getUniquePtr/
// ffAvoxPacket/ffIoError/ff*Codec 映射, 与 IOParseFF 完全同源
#include "avox_ffmpeg/FFHelper.hpp"

namespace avox {

// 磁力/BT 数据源: libtorrent顺序下载 + FFmpeg自定义avio直读piece解封装
// 解封装/包处理管线与IOParseFF一致(AVSource基类processPacket等全复用),
// 差异仅在输入侧: 用avio_alloc_context的read/seek回调对接TorrentEngine,
// 不经过协议层, 无本地HTTP中转。
class IOParseTorrent : public AVSource, public RunTask {
 public:
  IOParseTorrent();
  virtual ~IOParseTorrent();

 protected:
  // 引擎与流上下文(fmtCtx的CUSTOM_IO下pb不随close释放, 所有权在本类)
  std::unique_ptr<TorrentEngine> engine;
  AVFormatContextPtr fmtCtx = nullptr;
  // 自定义avio缓冲(av_malloc分配, close时统一释放)
  unsigned char* ioBuffer = nullptr;
  // avio上下文(fmtCtx仅持有pb指针)
  AVIOContext* ioCtx = nullptr;
  // 当前读取游标(相对目标文件的绝对字节偏移)
  int64_t ioPosition = 0;
  AudioDesc audioDesc = {};
  bool bAACExtradata = false;
  // seek窗口内打断阻塞读/av_read_frame(线程安全)
  std::atomic<bool> bInterruptRead{false};
  // IO线程已退出av_read_frame的确认(seekTo等它再操作fmtCtx)
  std::atomic<bool> bIoPausedAck{false};

 private:
  // 解析轨道并派发配置包(SPS/PPS/VPS/ASC)
  bool parseStream(int32_t streamId, AVCodecParameters* codecpar);
  // H26X extradata -> vconfig包
  void parseH26xConfig(int32_t streamId, const uint8_t* extradata,
                       int32_t size, AVCodecID codecId);
  // AAC ASC -> aconfig包
  void parseAACConfig(int32_t streamId, const uint8_t* extradata,
                      int32_t size);
  // open前的option快照(torrent.*键读取)
  void applyTorrentOptions();
  // 释放自定义avio缓冲与上下文(CUSTOM_IO下pb所有权在本类)
  void releaseIoContext();

 protected:
  // RunTask: 解封装读循环
  virtual void onRunTask() override;

 public:
  // 基类AVSource::open置位后回调: 启动引擎+建流
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
  // ---- avio_alloc_context 回调(静态转发) ----
  static int ioReadPacket(void* opaque, uint8_t* buf, int bufSize);
  static int64_t ioSeek(void* opaque, int64_t offset, int whence);
  // interrupt_callback: seek窗口内打断in-flight读
  static int decodeInterruptCb(void* ctx);
  bool interruptIo() const { return bInterruptRead.load(); }
  // 引擎配置项(torrent.* option缓存)
  TorrentEngine::Config engineCfg = {};
};
}
