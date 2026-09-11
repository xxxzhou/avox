#pragma once

#include <atomic>
#include <memory>

#include "avox/module/RunTask.hpp"
#include "avox/source/AVSource.hpp"
// FFmpeg 辅助层(已导出): AVFormatContextPtr/AVPacketPtr/getUniquePtr/
// ffAvoxPacket/ffIoError/ff*Codec 映射, 与 IOParseTorrent 完全同源
#include "avox_ffmpeg/FFHelper.hpp"

// libsmb2 预编译头(库仓, 见 cmake/FindLibsmb2.cmake)
#include <smb2/smb2.h>
#include <smb2/libsmb2.h>

namespace avox {

// SMB 数据源: libsmb2 直读 + FFmpeg自定义avio解封装。
// 解封装/包处理管线与IOParseFF/IOParseTorrent一致(AVSource基类processPacket等
// 全复用), 差异仅在输入侧: avio_alloc_context的read/seek回调对接libsmb2
// 同步API(smb2_pread/smb2_lseek), 无本地中转。
// URL: smb://[user:pass@]host[:port]/share/path(鉴权嵌userinfo, 与DAV同风格)。
class IOParseSmb : public AVSource, public RunTask {
 public:
  IOParseSmb();
  virtual ~IOParseSmb();

 protected:
  // libsmb2 会话与文件句柄(生命周期归本类, 播放期常驻)
  struct smb2_context* smb = nullptr;
  struct smb2fh* fh = nullptr;
  // 目标文件大小(smb2_fstat; AVSEEK_SIZE 与 EOF 判定)
  uint64_t fileSize = 0;
  // 流上下文(fmtCtx的CUSTOM_IO下pb不随close释放, 所有权在本类)
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
  // 入口 URL 解析产物
  struct UrlParts {
    std::string user = "";
    std::string pass = "";
    std::string host = "";
    int port = 445;
    std::string share = "";
    std::string path = "";  // share 内文件路径(不带首'/')
  };
  // 解析播放 URL(非法返回 false)
  static bool parseUrl(const std::string& url, UrlParts* out);
  // 解析轨道并派发配置包(SPS/PPS/VPS/ASC)
  bool parseStream(int32_t streamId, AVCodecParameters* codecpar);
  // H26X extradata -> vconfig包
  void parseH26xConfig(int32_t streamId, const uint8_t* extradata,
                       int32_t size, AVCodecID codecId);
  // AAC ASC -> aconfig包
  void parseAACConfig(int32_t streamId, const uint8_t* extradata,
                      int32_t size);
  // 释放自定义avio缓冲与上下文(CUSTOM_IO下pb所有权在本类)
  void releaseIoContext();

 protected:
  // RunTask: 解封装读循环
  virtual void onRunTask() override;

 public:
  // 基类AVSource::open置位后回调: 连接共享+开文件+建流
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
};

}
