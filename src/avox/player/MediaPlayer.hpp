#pragma once

#include <atomic>

#include "../AvoxPlayer.h"
#include "../module/RunTask.hpp"
#include "../module/TaskTrack.hpp"
#include "../muxer/MediaMuxer.hpp"
#include "../muxer/RawMuxer.hpp"
#include "../source/AVSource.hpp"
#include "../subtitle/SubtitleView.hpp"
#include "../video/Window.hpp"
#include "AudioTrack.hpp"
#include "BasePlayer.hpp"
#include "MPPingback.hpp"
#include "VideoTrack.hpp"

namespace avox {

// 把所有播放相关对象，track,render创建与释放都尽量统一到播放器自身线程上执行
// 减少相关对象需要同步导致的问题
// 注意一定要先调用onOpen，然后再能调用onPacket
// 只有先onOpen再能得到trackid,streamid与流对应关系
class MediaPlayer : public IMediaPlayer,
                    public IAVSourceOb,
                    public IMuxerOb,
                    public BasePlayer,
                    public RunTask,
                    public TaskTrack {
 public:
  MediaPlayer();
  virtual ~MediaPlayer();
  // TaskTrack：自身即大对象实例
  virtual TaskTrack* asTaskTrack() override { return this; }
  virtual const char* getTrackName() override;

 protected:
  // 埋点
  std::unique_ptr<MPPingQueue> mpPingback;
  // 解网络协议
  std::unique_ptr<AVSource> ioSource;
  std::unique_ptr<AVSource> ioTest;
  // 视频解码(缓存数据都在这里面)
  std::vector<VideoTrackPtr> videoTracks;
  // 音频解码(缓存数据都在这里面)
  std::vector<AudioTrackPtr> audioTracks;
  // 字幕显示
  std::unique_ptr<SubtitleView> subtitleView;
  // 相对于track的外部时钟
  std::unique_ptr<Clock> clock = nullptr;
  // 同步类型
  // 选择外部,是否需要根据队列数调整的speed[check_external_clock_speed])
  SyncType syncType = SyncType::audio;
  SyncType preSyncType = SyncType::none;
  // IO完成，如果IO完成了，当解码使用完后，状态切换成完成
  bool bIOComplete = false;

  // 设定延迟,用于平滑网络波动造成的卡顿
  // 最大值应该小于track队列最大-最小的时间差,否则永远满足不了播放条件
  // 值低，应对网络波动卡顿会频繁，但是延迟低，并且每次buffing时间短
  // 值高，应对网络波动卡顿会少，但是延迟高，并且每次buffing时间也长
  // 要不要加个自动调整功能？
  int64_t delayMs = 1000;
  // cspeed只记录用户设置的速度，不记录自动调整的速度
  double cspeed = 1.0;
  IoPlan selectIO = IoPlan::ffmpeg;
  bool bHardDecode = true;
  // 分拆包
  std::vector<AvoxPacket> spiltBufs;
  // 把IO数据封装复用到文件或是网络流
  std::unique_ptr<MediaMuxer> mediaMuxer = nullptr;
  // 把解码后的数据经图像处理后再复用
  std::unique_ptr<RawMuxer> rawMuxer = nullptr;
  // rawmuxer记录时,windows平台需要输出YUV
  bool bMuxerCpu = false;
  // 检查Buffing状态时间，超过10s可能需要关闭(torrent源默认放宽, 可经选项覆盖)
  TickChecker bufferChecker = {10000};
  // 业务是否显式设置过缓冲超时(未设置时 torrent 源自动放宽到 30s)
  bool bufferingTimeoutUserSet = false;
  // 记录seekPts
  int64_t seekPts = 0;
  // 外部调用时间与seek都在外面,在seek时记录
  // 在外部调用播放时间时,需要第一时间判定seek状态及时间
  // bSeeking 生命周期: seek() 置 true → cmdSeek 成功后保持 → compateIoTime 见真实位置追平目标后解除
  // 期间 getPosition 报 seekPts(目标): seek 落点在目标前一个关键帧(backward seek), 追平前报目标避免进度条回弹
  std::atomic<bool> bSeeking = false;
  // 追平判定守卫: 是否已观察到真实落点(renderTime 曾 < seekPts)
  // updateSeekTime 把时钟乐观置为目标, 首个渲染帧才拉到落点; 只有见过回落再爬回目标才解除 bSeeking,
  // 否则乐观时钟(=目标值)会提前解除, 回弹仍在
  std::atomic<bool> bSeekSeenLanding = false;
  // 兜底解除起始墙钟(ms, 0=未生效): 落点恰好等于目标、无回落时, 防进度条永远钉在目标
  std::atomic<int64_t> bSeekingStartMs = 0;
  // 是否记录解帧信息,这个可能对音频渲染有影响,卡卡的
  bool bLogDFrame = false;
  // 是否记录渲染帧信息
  bool bLogRFrame = false;

 protected:
  // 直播模式下，是否开启低延迟，数据太多会自动快播
  bool bLowLatency = false;
  // 低延迟播放时，队列数据多了,自动快速播放速度
  double autoSpeed = 1.1;
  // IO状态
  PBIOStatus ioStatus = {};
  // 音频或视频的队列状态
  PBQueueStatus audioStatus = {};
  PBQueueStatus videoStatus = {};
  // 音频与视频是渲染与IO对齐否
  bool bAVAlign = false;
  // 因未知异常，导致二边IO包一边满,一边空
  // 这样一直buffing也不行，直接全清空
  // 等待下一个配置帧或I帧来继续进队列
  bool bClearFlag = false;
  // I帧模式：只有I帧没有P/B帧
  bool bIFrameMode = false;
  // 4倍以上速度时是否只处理I帧(进包时丢弃P/B与音频,恰好4倍仍全量),默认true
  bool bIFrameOnlyGt4 = true;
  // 上面选项与当前倍速合成的生效真值(选项开 且 cspeed>4)。
  // onPacket 据此丢 P/B + 音频包, 音频解码线程据此免掉解码超时判定 —— 两处必须
  // 同一判据: 若各自写 speed>4, 选项关掉时音频包正常流却仍免超时, 真实解码故障
  // 会被永久吞掉。播放器线程写(cmdSpeed / onOptionChange), 解码线程读, 故用 atomic
  std::atomic<bool> bIFrameOnlyActive{false};

  // IO在播放器线程,全在播放器线程计算
  double ioPrecent = 0.0;
  int64_t ioDuration = 0;
  int64_t ioBaseTime = 0;
  int64_t renderTime = 0;
  float ioVideoLoss = 0.0f;
  float ioAudioLoss = 0.0f;

 public:
  MPPingQueue* getMPPingback() { return mpPingback.get(); }
  AVSource* getSource() { return ioSource.get(); }
  RawMuxer* getRawMuxer() { return rawMuxer.get(); }
  SubtitleView* getSubtitleView() { return subtitleView.get(); }
  SyncType getSyncType() { return syncType; }
  // I帧渲染模式: 源级I帧模式(源只发I帧) 或 >4x只解I帧生效(bIFrameOnlyActive)时
  // 都为true。渲染节奏/音频静音/外部调用统一用这个判断"当前是否处于稀疏I帧渲染",
  // 避免全包流>4x丢P/B后帧队列只剩I帧却仍走普通渲染路径(长GOP被误判跳变/丢帧)
  bool getIFrameMode() { return bIFrameMode || bIFrameOnlyActive.load(); }
  // >4x只I帧是否生效中(选项开 且 倍速>4): onPacket 丢 P/B+音频包的判据,
  // 音频解码线程(ADecoderTask)据此免掉解码超时判定。可跨线程读
  bool iframeOnlyActive() const { return bIFrameOnlyActive.load(); }
  // 重算 >4x 只I帧的生效真值: 选项变更(onOptionChange)与倍速变更(cmdSpeed)后各调一次。
  // 只在播放器线程调用, 故读 bIFrameOnlyGt4 / cspeed 不需要同步
  void updateIFrameOnly() {
    bIFrameOnlyActive.store(bIFrameOnlyGt4 && cspeed > 4);
  }
  // 外部时钟,用于外部控制同步
  Clock* getExtClock() { return clock.get(); }
  // 主时钟,可能是外部时钟clock,也可能是videoTracks/audioTracks的时钟
  Clock* getMainClock();

  void onDecodeError(AVTrack* track, DecodeResult error);

  // IMuxerOb
 public:
  virtual void onMuxerOpen(MediaMuxer* muxer) override;
  virtual void onMuxerClose() override;

  // IAVSourceOb 回调
 public:
  // IO打开到得到基本Track信息,需要等待处理完回调
  virtual void onReady() override;
  virtual void onSyncPts() override;
  virtual void onClose() override;
  virtual void onComplete() override;
  virtual void onError(AVError error, const char* msg) override;
  virtual void onPacket(const AvoxPacket& packet) override;
  virtual void onIFrameMode(bool bIFrameMode) override;

  // JsonOption
 public:
  // 转到播放器线程向外广播
  virtual void onOptionChange(const char* key, ArgType option) override;

 protected:
  virtual void onSetState() override;

 public:
  const std::vector<VideoTrackPtr>& getVideoTracks() { return videoTracks; }
  const std::vector<AudioTrackPtr>& getAudioTracks() { return audioTracks; }
  bool getHardDecode() { return bHardDecode; }
  bool logDFrame() { return bLogDFrame; }
  bool logRFrame() { return bLogRFrame; }

 public:
  virtual IOption* getOption() override;
  virtual void setPingbackOb(IPingbackOb* ob) override;
  virtual void setIoPlan(IoPlan plan) override;
  virtual void setHardDecode(bool hard) override;
  virtual ISurfaceRender* getSurfaceRender() override;
  virtual IAudioRender* getAudioRender() override;
  virtual IMediaMuxer* getMuxer(bool bTranscode) override;
  virtual ISubtitle* getSubtitle() override;
  // 这些可以多线程上调用，统一压入到命令队列中，然后给播放器线程执行
  virtual void open(const char* url) override;
  virtual void close() override;
  virtual void seek(int64_t pos) override;
  virtual void pause() override;
  virtual void resume() override;
  virtual void speed(double speed) override;

  virtual PlayerState getState() override;
  virtual double getProcess() override;
  virtual int64_t getDuration() override;
  virtual int64_t getPosition() override;
  virtual int64_t getStartTime() override;

  virtual ISourceInfo* getSourceInfo() override;
  virtual double getRate(TrackType type, bool bAvg) override;
  virtual float getLossRate(TrackType type) override;
  virtual double getFps() override;

 protected:
  // 播放器线程
  virtual void onRunTask() override;
  // buffing状态是否可以切换到播放状态
  // 是否进入快播，减少延迟
  void tick();
  // IO对象访问限定到播放器线程中
  void compateIoTime();
  void collectStatus();
  float getIoLossRate(TrackType type);

 private:
  // 播放器线程中具体执行命令
  void cmdOpen(OpenCommandPtr cmd);
  void cmdReady();
  void cmdPlaying();
  void cmdClose();
  void cmdPause(PauseCommandPtr cmd);
  void cmdComplete();
  void cmdSeek(SeekCommandPtr cmd);
  void cmdBuffering();
  void cmdSpeed(SpeedCommandPtr cmd);
  void cmdOption(OptionCommandPtr cmd);
  void cmdSetWindow(SetWindowCommandPtr cmd);
  // 播放器线程发出需要重置的命令，得到解码器线程执行
  void cmdResetDecode(ResetDecodeCommandPtr cmd);
  // 播放器线程重启解码对象，保持播放器内对象生死都在播放器内线程
  void cmdResetDecodeComplete();
  void cmdSyncPts();
  void cmdIFrameMode(IFrameModeCommandPtr cmd);

 public:
  // 解码队列遇到结束信号，调用
  void complete();
  // 解码队列没有数据了，进入buffer状态
  void renderFrame(AVTrack* track, bool bGetFrame);
  // 原解码器关闭后，通知播放器线程
  void onResetDecoder();
  // 特殊情况，不需要同步，需要记录同步状态，后面恢复
  // 如音频与视频起点差异大，或者倍速很大，同步无意义
  void closeSync(bool bClose);
  // 有二种可能，用户调整的，或者播放器控制延迟自动调整的
  // 用户控制的，会记录到cspeed上，播放器自动调整的不会
  void setSpeed(double speed);
  double getSpeed();

 private:
  // 输出环形缓冲区数据信息，用于排查问题，可用于在状态变化的地方
  void recordRingBuffer();
  // 得到队列缓存时长，包队列尾-帧队列头
  int64_t getQueueTime(TrackType type);
  // 播放器播放状态下常态，包含playing/pause/buffering
  bool normalState();
  // seek,关闭时调用,清空数据
  void flush();
  //
  void updateSeekTime(int64_t seekTime);
  // 用于seek时,IO队列暂时不可用,直接调用IO暂停可能导致RTSP的seek不了
  void pauseIOPacket(bool pause);
  // IO不暂停，保持下载，解码器也不暂停
  // track暂停(主要是渲染暂停,时钟暂停)
  void pauseRender(bool pause);
  // 暂停解码器，主要是seek时,暂停解码线程好清理队列数据
  void pauseDecoder(bool pause);
};

}