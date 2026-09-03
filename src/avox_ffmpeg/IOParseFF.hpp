#pragma once

#include "FFHelper.hpp"
#include "avox/module/Json.hpp"
#include "avox/module/JsonOption.hpp"
#include "avox/module/RunTask.hpp"
#include "avox/source/AVSource.hpp"

namespace avox {

class IOParseFF : public AVSource, public RunTask {
public:
  IOParseFF();
  virtual ~IOParseFF();

protected:
  AVFormatContextPtr fmtCtx = nullptr;
  AVBSFContextPtr bsf = nullptr;
  std::vector<uint8_t> aacData;
  AudioDesc audioDesc = {};
  // 如果是直播流,可能存在aac extradata,需要把首帧的adts加入
  bool bAACExtradata = false;
  // seek 窗口内为 true, 打断 IO 线程阻塞的 av_read_frame (线程安全)
  std::atomic<bool> bInterruptRead{false};
  // close/析构时置位, 打断 IO 线程阻塞的 av_read_frame: 不打断则 stopTask 的
  // join 干等到对端断开(回放流推完服务端不主动断, 曾挂 5 分钟)
  std::atomic<bool> bStopIo{false};
  // IO 线程已退出 av_read_frame 的确认 (seekTo 等它再操作 fmtCtx)
  std::atomic<bool> bIoPausedAck{false};

private:
  // 解析IO流媒体格式
  bool parseStream(int32_t streamId, AVCodecParameters *codecpar);
  bool parseH26xConfig(int32_t streamId, const uint8_t *extradata, int32_t size,
                       AVCodecID codeId);
  void parseAACConfig(int32_t streamId, const uint8_t *extradata, int32_t size);
  // RunTask
protected:
  virtual void onRunTask() override;

public:
  // 初始化，打开文件/网络流
  virtual bool onOpen() override;
  // 关闭
  virtual void onClose() override;
  // seek 前预通知: 立即打断 IO 线程阻塞中的 av_read_frame(与 seekTo 内设 bInterruptRead 同效,
  // 但提前到 cmdSeek 撤背压的时刻, 快源读线程不会狂奔到 EOF)
  virtual void preSeek() override;
  // 暂停
  virtual void pause(bool bFlag) override;
  // 能seek吗?
  virtual SeekType seekType() const override;
  // 0-1
  virtual void seekTo(double progress) override;
  // 毫秒时间
  virtual bool seekTo(int64_t pos) override;
  // 返回总时长，单位毫秒
  virtual int64_t duration() const override;
  // 返回进度，0~1
  virtual double progress() const override;

public:
  virtual bool vaild() { return running(); }
  virtual void onSpeed() override;
  // interrupt_callback 用: seek 窗口或 close 后返回 true 打断 in-flight 读
  bool interruptIo() const { return bInterruptRead.load() || bStopIo.load(); }
};

}