#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "avox/module/RunTask.hpp"
#include "avox/source/AVSource.hpp"
// FFmpeg 辅助层(已导出): AVFormatContextPtr/AVPacketPtr/getUniquePtr/
// ffAvoxPacket/ffIoError/ff*Codec 映射, 与 IOParseSmb/IOParseTorrent 完全同源
#include "avox_ffmpeg/FFHelper.hpp"

namespace httplib {
class Client;
}

namespace avox {

// WebDAV 直链数据源(a05-T2): http(s) range 读 + 预读窗口 + FFmpeg自定义avio
// 解封装。解封装/包处理管线与 IOParseSmb/IOParseFF 一致(AVSource 基类全复用),
// 差异仅在输入侧: avio read 回调对接 cpp-httplib 的 Range GET, 单线程预读
// 窗口(顺序读摊薄请求次数, seek 落窗口内零请求)。
// URL: http(s)://[user:pass@]host[:port]/path(DavSource::entryUrl 直链原样),
// 另收 dav://(映射http)/davs://(映射https) 别名; userinfo 百分号编码态入,
// Basic 认证前解码。
class IOParseDav : public AVSource, public RunTask {
 public:
  IOParseDav();
  virtual ~IOParseDav();

 protected:
  // 会话客户端(keep-alive, 连接死时重建一次)
  std::unique_ptr<httplib::Client> client;
  // 生效播放 URL(onOpen 取基类 url; refresh 重取后更新, ensureClient 按此解析)
  std::string curUrl;
  // 鉴权 Header 注入(断链桥重取时自 DavSource 会话拷贝, 空=不携带)
  std::string authKey;
  std::string authVal;
  // 请求路径(ensureClient 时从 URL 解析缓存, 保持百分号编码原样)
  std::string reqPath;
  // 目标文件大小(首个 range 响应的 Content-Range total; EOF 与 AVSEEK_SIZE)
  uint64_t fileSize = 0;
  // 预读窗口字节数(默认 kLookaheadSize; 测试可经 option "remote.dav.lookahead"
  // 调小, 强制窗口频繁重填 —— 小素材否则一窗吃尽, 断链面永远咬不到)
  uint64_t lookaheadSize = 0;  // 0 = 未配置, refillWindow 取默认
  // 流上下文(fmtCtx的CUSTOM_IO下pb不随close释放, 所有权在本类)
  AVFormatContextPtr fmtCtx = nullptr;
  // 自定义avio缓冲(av_malloc分配, close时统一释放)
  unsigned char* ioBuffer = nullptr;
  // avio上下文(fmtCtx仅持有pb指针)
  AVIOContext* ioCtx = nullptr;
  // 当前读取游标(相对目标文件的绝对字节偏移)
  int64_t ioPosition = 0;
  // 预读窗口: winBuf 覆盖 [winStart, winStart+winValid) 这段文件字节
  std::vector<uint8_t> winBuf;
  uint64_t winStart = 0;
  uint64_t winValid = 0;
  AudioDesc audioDesc = {};
  bool bAACExtradata = false;
  // seek窗口内打断阻塞读/av_read_frame(线程安全)
  std::atomic<bool> bInterruptRead{false};
  // IO线程已退出av_read_frame的确认(seekTo等它再操作fmtCtx)
  std::atomic<bool> bIoPausedAck{false};
  // EOF 停放(IOParseFF 同款): EOF 不退循环, seekTo 成功后复位续读。
  // 小文件起播即到 EOF, 线程一退 seek 只挪 demuxer 位置无人再读, 管道静止
  std::atomic<bool> bEof{false};
  std::atomic<bool> bEofReset{false};     // seekTo 成功后置位: 叫醒停放的读线程
  std::atomic<bool> bEofNotified{false};  // onComplete 每次 EOF 只报一次

 private:
  // 入口 URL 解析产物(requestPath 保持百分号编码原样)
  struct UrlParts {
    bool https = false;
    std::string user = "";
    std::string pass = "";
    std::string host = "";
    int port = 0;  // 0=按 scheme 默认
    std::string path = "";
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
  // range 拉取 [start, end] 入 out: 返回 206=成功, 其他=http 状态码, 0=连接级失败
  // (内部对连接死重建会话重试一次)
  int fetchRange(uint64_t start, uint64_t end, std::vector<uint8_t>* out);
  // 预读窗口未覆盖 ioPosition 时重新拉取; 失败按 a05-T3 分类自愈
  // (瞬时类退避重试, 401/403/404/410 走断链桥 refresh 换新直链), 全部
  // 尝试耗尽才返回 false —— 上层 avio 拿 EIO 走既有终错路径
  bool refillWindow();
  // 退避等待(1s/2s/4s, 分片轮询 bInterruptRead; 被打断返回 false)
  bool backoffSleep(int retry);
  // 断链桥: 找回产出当前 URL 的 DavSource 会话并 refresh 换新直链(携其鉴权
  // Header); 无桥/重取失败返回 false
  bool bridgeRefresh();
  // 连接级错误后重建会话客户端(各请求独立重试一次)
  bool ensureClient();

 private:
  // ---- avio_alloc_context 回调(静态转发) ----
  static int ioReadPacket(void* opaque, uint8_t* buf, int bufSize);
  static int64_t ioSeek(void* opaque, int64_t offset, int whence);
  // interrupt_callback: seek窗口内打断in-flight读
  static int decodeInterruptCb(void* ctx);
  bool interruptIo() const { return bInterruptRead.load(); }

 protected:
  // RunTask: 解封装读循环
  virtual void onRunTask() override;

 public:
  // 基类AVSource::open置位后回调: 建会话+探测大小+建流
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
};

}
