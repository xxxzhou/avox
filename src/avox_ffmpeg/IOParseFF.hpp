#pragma once

#include <deque>
#include <memory>
#include <set>

#include "FFHelper.hpp"
#include "avox/module/Json.hpp"
#include "avox/module/JsonOption.hpp"
#include "avox/module/RunTask.hpp"
#include "avox/source/AVSource.hpp"

namespace avox {

class PgsDecoder;

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
  // 编码不支持而跳过的音频流号(parseStream 处): 包循环按流号丢弃, 不再株连
  // 整条音频(a08)
  std::set<int32_t> skipAudioStreams;
  // seek 窗口内为 true, 打断 IO 线程阻塞的 av_read_frame (线程安全)
  std::atomic<bool> bInterruptRead{false};
  // close/析构时置位, 打断 IO 线程阻塞的 av_read_frame: 不打断则 stopTask 的
  // join 干等到对端断开(回放流推完服务端不主动断, 曾挂 5 分钟)
  std::atomic<bool> bStopIo{false};
  // IO 线程已退出 av_read_frame 的确认 (seekTo 等它再操作 fmtCtx)
  std::atomic<bool> bIoPausedAck{false};
  // EOF 停放(见 onRunTask): 读到尾不拆读线程 —— 拆了之后 seek 就是"无人读"的
  // 僵尸态。本地短文件最容易踩: 点播包队列上限 1000 包 ≥ 整文件包数, 起播毫秒级
  // 读满 → EOF → 之后任何 seek 都再也拿不到数据(录制产物缺失/画面静止)。
  std::atomic<bool> bEofReset{false};     // seekTo 成功后置位: 叫醒停放的读线程
  std::atomic<bool> bEofNotified{false};  // onComplete 每次 EOF 只报一次
  // seek 后等关键帧门闸: RM 等脏索引封装的 avformat_seek_file 落点不可靠,
  // 解码器从 P/B 帧起步缺参考 → 花屏散块(方子传CD1 断点续播必现)。seek 成功
  // 置位, 读循环丢视频包直到首个 KEY 包; 500 包防呆防不打 KEY 标志的封装
  std::atomic<bool> bWaitKeyframe{false};
  int32_t waitKeyframeDrops = 0;  // IO 线程专用计数
  // seek 落点校验预读包: 校验须直读 fmtCtx 才知道落点, 读出的包不能丢(音轨
  // 开头尤其不能缺), 暂存待读循环优先原序下发; 非关键视频包校验期即丢弃(与
  // 门闸同语义), 尾包是首个关键视频包。仅 seekTo 持 fmtCtx 独占期(IO 线程
  // bIoPausedAck 停放)写入, 读循环单清
  std::deque<AVPacket*> seekStash;
  // PGS 解码器(首个 PGS 流在轨扫描期即建): 以流索引喂包门控
  std::unique_ptr<PgsDecoder> pgsDec = nullptr;
  int32_t pgsStreamId = -1;  // PGS 解码器对应的 ffmpeg 流索引(非局部轨号)

private:
  // 解析IO流媒体格式
  bool parseStream(int32_t streamId, AVCodecParameters *codecpar);
  // avformat_open_input(重)打开, fmtCtx 接管; FFmpeg9 保底补查须重开, 不能同上下文二次探测
  int reopenInput();
  // PGS 位图字幕解码(§3.6): 选中该轨时 IO 循环喂包, 出 RGBA 画布
  bool parsePgsFrame(int32_t streamId, const AVPacket* pkt, int64_t ptsMs);
  // seek 落点校验: 直读 fmtCtx 到首个视频包记落点(ms), 到首个关键视频包停
  // (包数/时长兜底); 读出的包进 seekStash 回灌. 返回是否拿到视频落点
  bool verifySeekLanding(int32_t videoStreamId, int64_t& landedMs,
                         bool& sawKey);
  // 清空 seekStash(av_packet_free)
  void clearSeekStash();
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