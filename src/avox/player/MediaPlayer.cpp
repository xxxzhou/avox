#include "MediaPlayer.hpp"

#include <thread>

#include "../AvoxVersion.h"
#include "../module/AvoxManager.hpp"
#include "../module/LogHelper.hpp"
#include "../module/ModuleMgr.hpp"
#include "../module/OptionKey.hpp"

namespace avox {

// 通用轨道检查函数
template <typename Tracks, typename CheckFunc>
bool checkTracks(const Tracks& tracks, CheckFunc&& check) {
  for (const auto& track : tracks) {
    if (track && track->vaild() && check(track)) {
      return true;
    }
  }
  return false;
}

const char* MediaPlayer::getTrackName() { return "MP"; }

MediaPlayer::MediaPlayer() {
  taskName = "media player command";
  option["commit"] = AVOX_COMMIT_HASH;
  // 假定一个播放器里最多能处理4路音频与4路视频
  // 打开track，放这主要是统一在播放器线程管理对象开/关
  audioTracks.resize(AVOX_MAX_TRACK);
  videoTracks.resize(AVOX_MAX_TRACK);
  // HighClock hclock = {};
  for (int32_t i = 0; i < AVOX_MAX_TRACK; ++i) {
    audioTracks[i] = std::make_shared<AudioTrack>();
    // log(LogLevel::info, "mp pipe cost 1:", hclock.recordLast());
    videoTracks[i] = std::make_shared<VideoTrack>();
    // log(LogLevel::info, "mp pipe cost 2:", hclock.recordLast());
  }
  subtitleView = std::make_unique<SubtitleView>();
  subtitleView->setAsrMode(AsrMode::ptsSync);
  clock = std::make_unique<Clock>();
  // 埋点单独线程
  mpPingback = std::make_unique<MPPingQueue>();
  // 默认打开低延迟
  // bLowLatency = true;
  autoSpeed = 1.2;
  // 默认音频为主时钟
  syncType = SyncType::audio;
  selectIO = IoPlan::ffmpeg;
#ifdef __ANDROID__
  bHardDecode = true;
  selectIO = IoPlan::zlmediakit;
#elif __APPLE__
  bHardDecode = true;
  selectIO = IoPlan::ffmpeg;
#else
  bHardDecode = true;
  selectIO = IoPlan::ffmpeg;
#endif
  mediaMuxer = std::make_unique<MediaMuxer>();
  rawMuxer = std::make_unique<RawMuxer>();
  mpCommands.setMaxSize(100);
  // 专门用来处理改变播放状态的线程
  startTask();
  // bLogDFrame = true;
  // bLogRFrame = true;
}

MediaPlayer::~MediaPlayer() {
  LOGFLF(LogLevel::info, "destroy media player");
  stopTask();
}

void MediaPlayer::onOptionChange(const char* key, ArgType option) {
  // bool bMpOption = true;
  if (equalsIgnoreCase(key, AVOX_MP_LOW_LATENCY_BOOL)) {
    bLowLatency = getBool(key);
    LOGFLF(LogLevel::info, "option:", key, " change:", bLowLatency);
  } else if (equalsIgnoreCase(key, AVOX_MP_LL_SPEED_DOUBLE)) {
    autoSpeed = getDouble(key);
    LOGFLF(LogLevel::info, "option:", key, " change:", autoSpeed);
  } else if (equalsIgnoreCase(key, AVOX_MP_DELAY_MS_INT)) {
    delayMs = getInt(key);
    LOGFLF(LogLevel::info, "option:", key, " change:", delayMs);
  } else if (equalsIgnoreCase(key, AVOX_MP_BUFFERING_TIMEOUT_MS_INT)) {
    bufferingTimeoutUserSet = true;
    bufferChecker.setDelay(getInt(key));
    LOGFLF(LogLevel::info, "option:", key, " change:", getInt(key));
  } else if (equalsIgnoreCase(key, AVOX_LOG_DECODER_FRAME_BOOL)) {
    bLogDFrame = getBool(key);
    LOGFLF(LogLevel::info, "option:", key, " change:", bLogDFrame);
  } else if (equalsIgnoreCase(key, AVOX_LOG_RENDER_FRAME_BOOL)) {
    bLogRFrame = getBool(key);
    LOGFLF(LogLevel::info, "option:", key, " change:", bLogRFrame);
  } else if (equalsIgnoreCase(key, AVOX_MP_SYNC_TYPE_INT)) {
    int v = getInt(key);
    switch (v) {
      case 0:
        syncType = SyncType::none;
        break;
      case 1:
        syncType = SyncType::audio;
        break;
      case 2:
        syncType = SyncType::video;
        break;
      case 3:
        syncType = SyncType::external;
        break;
      default:
        break;  // 非法值保持不变
    }
    LOGFLF(LogLevel::info, "option:", key, " change:", v);
  } else if (equalsIgnoreCase(key, AVOX_MP_IFRAME_ONLY_GT4_BOOL)) {
    bIFrameOnlyGt4 = getBool(key);
    updateIFrameOnly();
    LOGFLF(LogLevel::info, "option:", key, " change:", bIFrameOnlyGt4);
  }
}

void MediaPlayer::onSetState() {
  // 埋点记录状态变化
  PBStateChange pb = {};
  pb.state = state;
  pb.preState = preState;
  pushPB<MPPBType::State>(mpPingback.get(), pb);
  // 记录track的状态
  recordRingBuffer();
}

Clock* MediaPlayer::getMainClock() {
  if (syncType == SyncType::audio) {
    for (const auto& atrack : audioTracks) {
      if (atrack && atrack->vaild()) {
        return atrack->getClock();
      }
    }
  } else if (syncType == SyncType::video) {
    for (const auto& vtrack : videoTracks) {
      if (vtrack && vtrack->vaild()) {
        return vtrack->getClock();
      }
    }
  }
  // 没有找到,返回播放器时钟
  return clock.get();
}

void MediaPlayer::onDecodeError(AVTrack* track, DecodeResult error) {
  // 记录解码错误
  PBMediaIssue pb = {};
  pb.errorType = MediaObject::decode;
  pb.code = (int32_t)error;
  pb.msg = getDecodeResultStr(error);
  pb.trackType = track->getTrackType();
  pushPB<MPPBType::MediaIssue>(mpPingback.get(), pb);
  MPOB::dispatch(&IMediaPlayerOb::onDecodeError, track->getTrackType(), error);
  if (track->getTrackType() == TrackType::video &&
      error == DecodeResult::timeout) {
    // 关闭重开
    close();
  }
}

void MediaPlayer::onMuxerOpen(MediaMuxer* muxer) {
  if (!ioSource) {
    return;
  }
  bool bRaw = false;
  if (muxer == rawMuxer.get()) {
    bRaw = true;
  }
  auto vtracks = ioSource->getVideoTracks();
  auto atracks = ioSource->getAudioTracks();
  // 如果用RAW录制,需要使用渲染的视频信息
  if (vtracks.size() > 0) {
    if (bRaw) {
      WindowRender* winRender = videoTracks[0]->getSurfaceRender();
      YuvType outYuv = winRender->getOutYuv();
      // windows平台需要输出YUV数据
#ifdef WIN32
      // 如果没打开YUV输出
      if (outYuv == YuvType::other) {
        winRender->enableYuvOut(YuvType::yuv420P);
        outYuv = YuvType::yuv420P;
        bMuxerCpu = true;
      }
#endif
      vec2i outSize = winRender->getOutSize();
      if (outYuv != YuvType::other) {
        if (outYuv == YuvType::nv12) {
          muxer->setHardEncode(true);
        } else {
          muxer->setHardEncode(false);
        }
        VTrackDesc vdesc = {};
        vdesc.desc = vtracks[0].desc;
        vdesc.desc.type = outYuv;
        vdesc.desc.width = outSize.x;
        vdesc.desc.height = outSize.y;
        muxer->setInVideoDesc(vdesc);
      }
    } else {
      muxer->setInVideoDesc(vtracks[0]);
    }
  }
  // 如果用RAW录制,需要使用解码后的音频信息
  if (atracks.size() > 0) {
    if (bRaw) {
      ATrackDesc adesc = {};
      adesc.desc = audioTracks[0]->getDecodeDesc();
      muxer->setInAudioDesc(adesc);
    } else {
      muxer->setInAudioDesc(atracks[0]);
    }
  }
  muxer->ready();
}
void MediaPlayer::onMuxerClose() {
  // 如果是因为录制打开的CPU模式，现在需要关闭
  if (bMuxerCpu) {
    WindowRender* winRender = videoTracks[0]->getSurfaceRender();
    winRender->disableYuvOut();
    bMuxerCpu = false;
  }
}

// 注意一定要先调用onOpen，然后再能调用onPacket
void MediaPlayer::onReady() {
  if (!ioSource) {
    return;
  }
  const auto& vTracks = ioSource->getVideoTracks();
  const auto& aTracks = ioSource->getAudioTracks();
  ioDuration = ioSource->duration();
  LOGFLF(LogLevel::info, "io duration:", ioDuration);
  // 设置
  for (int32_t i = 0; i < aTracks.size(); i++) {
    // 基本的初始化信息,对应track开始有效
    audioTracks[i]->setTrackDesc(aTracks[i]);
  }
  for (int32_t i = 0; i < vTracks.size(); i++) {
    videoTracks[i]->setTrackDesc(vTracks[i]);
  }
  // 通知播放器开始执行ready命令
  auto readCmd = createCommand<MPCommandType::Ready>();
  mpCommands.enqueueWait(readCmd);
  // 记录
  ioStatus.videoCount = ioSource->videoSize();
  ioStatus.audioCount = ioSource->audioSize();
}

void MediaPlayer::onSyncPts() {
  auto syncCmd = createCommand<MPCommandType::SyncPts>();
  mpCommands.enqueueWait(syncCmd);
}

void MediaPlayer::onIFrameMode(bool bIFrame) {
  auto iframeCmd = createCommand<MPCommandType::IFrameMode>(bIFrame);
  mpCommands.enqueueWait(iframeCmd);
}

void MediaPlayer::onClose() { close(); }

void MediaPlayer::onComplete() {
  LOGFLF(LogLevel::info, "av source complete");
  bIOComplete = true;
  // 记录IO完成
  PBMediaAction pb = {};
  pb.mediaObject = MediaObject::io;
  pb.action = MediaAction::complete;
  pushPB<MPPBType::MediaAction>(mpPingback.get(), pb);
}

void MediaPlayer::onError(AVError error, const char* msg) {
  if (error == AVError::none) {
    return;
  }
  if (error == AVError::endOfFile) {
    onComplete();
  } else {
    LOGFLF(LogLevel::warn, "av source error:", getAVErrorStr(error),
           " msg:", msg);
    MPOB::dispatch(&IMediaPlayerOb::onIoError, error, msg);
  }
  // 记录IO错误
  PBMediaIssue merror = {};
  merror.errorType = MediaObject::io;
  merror.code = (int32_t)error;
  merror.msg = msg;
  pushPB<MPPBType::MediaIssue>(mpPingback.get(), merror);
}

// 这里面IO线程会用到audioTracks/videoTracks
// 因为，需要先关闭IO线程，等IO线程关闭，此回调不在用，再关闭track
// 还有会调用RingBuffer的enqueueWait,可能会阻塞
void MediaPlayer::onPacket(const AvoxPacket& packet) {
  // packet.index在之前已经从全局av转化为对应a/v里的index
  int32_t index = packet.index;
  // 录制优先: 录制拿完整原始流, 不受播放倍速丢帧影响 — 否则 >4x
  // 录制只有I帧无音频 丢帧是播放策略, 不影响录制(录制内容随demux走,
  // 倍速下超前画面属预期)
  if (mediaMuxer->getState() == RecorderState::recording) {
    mediaMuxer->pushPacket(packet);
  }
  // 4倍以上只处理I帧: 丢弃视频P/B帧,只留I帧(配置帧照常),恰好4倍仍全量处理
  // 音频数据包直接丢弃: >4x音频不渲染(ARenderTask静音分支), 不喂AudioTrack队列,
  // 让音频彻底退出demux背压链 —
  // 否则音频管线消费慢→包队列(100)堆满→enqueueWait阻塞
  // 整个demux→视频I帧饿死(画面慢), 或消费过快→demux狂读(进度条闪).
  // 丢弃后demux只被 视频I帧管线按speed倍速自节流, 无需音频侧任何节流调参.
  // aconfig照常放行, 降回4x可恢复。判据是 bIFrameOnlyActive(选项+倍速合成的原子真值),
  // 音频解码线程用同一个判据免超时判定, 见 MediaPlayer.hpp
  if (bIFrameOnlyActive.load()) {
    PackType ptype = (PackType)packet.packtype;
    if (ptype == PackType::video && packet.frameType == 0) {
      return;
    }
    if (ptype == PackType::audio) {
      return;
    }
  }
  switch (packet.packtype) {
    case (int32_t)PackType::aconfig:
    case (int32_t)PackType::audio: {
      if (index >= audioTracks.size()) {
        LOGFLF(LogLevel::warn, "audio index out of range:", index);
        return;
      }
      if (!audioTracks[index]) {
        break;
      }
      if (packet.packtype == (int32_t)PackType::aconfig) {
      }
      audioTracks[index]->pushPacket(packet);
      break;
    }
    case (int32_t)PackType::vconfig:
    case (int32_t)PackType::video: {
      if (index >= videoTracks.size()) {
        LOGFLF(LogLevel::warn, "video index out of range:", index);
        return;
      }
      if (!videoTracks[index] || packet.data.size <= 4) {
        break;
      }
      videoTracks[index]->pushPacket(packet);
      break;
    }
    default:
      break;
  }
  // 已经报了EOF,但是又来数据了，那就算IO还在继续
  if (bIOComplete) {
    bIOComplete = false;
  }
}

IOption* MediaPlayer::getOption() { return this; }

void MediaPlayer::setPingbackOb(IPingbackOb* ob) {
  mpPingback->setPingbackOb(ob);
}

void MediaPlayer::setIoPlan(IoPlan plan) {
  if (selectIO == plan) {
    return;
  }
  // 记录设置
  PBMediaAction pb = {};
  pb.mediaObject = MediaObject::mediaplay;
  pb.trackType = TrackType::video;
  pb.action = MediaAction::config;
  string_format(pb.msg, "set IOPlan from ", getIoPlanStr(selectIO), " to ",
                getIoPlanStr(plan));
  pushPB<MPPBType::MediaAction>(mpPingback.get(), pb);
  // 选择IO方案
  selectIO = plan;
}

void MediaPlayer::setHardDecode(bool hard) {
  // 正在运行中
  if (!normalState()) {
    bHardDecode = hard;
    return;
  }
  // 重置解码器
  auto resetCmd = createCommand<MPCommandType::ResetDecode>(hard);
  mpCommands.enqueueWait(resetCmd);
  // 记录设置
  PBMediaAction pb = {};
  pb.mediaObject = MediaObject::mediaplay;
  pb.trackType = TrackType::video;
  pb.action = MediaAction::config;
  string_format(pb.msg, "set HardDecoder from ", bHardDecode, " to ", hard);
  bHardDecode = hard;
  pushPB<MPPBType::MediaAction>(mpPingback.get(), pb);
}

ISurfaceRender* MediaPlayer::getSurfaceRender() {
  int32_t index = 0;
  if (index < 0 || index >= videoTracks.size()) {
    return nullptr;
  }
  return videoTracks[index]->getSurfaceRender();
}

IAudioRender* MediaPlayer::getAudioRender() {
  int32_t index = 0;
  if (index < 0 || index >= audioTracks.size()) {
    return nullptr;
  }
  return audioTracks[index]->getAudioRender();
}

IMediaMuxer* MediaPlayer::getMuxer(bool bTranscode) {
  if (bTranscode) {
    return rawMuxer.get();
  }
  return mediaMuxer.get();
}

ISubtitle* MediaPlayer::getSubtitle() { return subtitleView.get(); }

PlayerState MediaPlayer::getState() { return state; }

double MediaPlayer::getProcess() { return ioPrecent; }

int64_t MediaPlayer::getDuration() { return ioDuration; }

int64_t MediaPlayer::getPosition() {
  // seek 中或 seek 成功后真实位置未追平目标: 报请求目标(意图反馈),
  // 避免"先到目标再闪回落点" 播放已结束/出错无"追平"可言, 直接报真实位置
  if ((bSeeking || state == PlayerState::seek) &&
      state != PlayerState::completed && state != PlayerState::stopped) {
    return seekPts;
  }
  return renderTime;
}

int64_t MediaPlayer::getStartTime() { return ioBaseTime; }

ISourceInfo* MediaPlayer::getSourceInfo() {
  if (ioSource) {
    return ioSource.get();
  }
  return nullptr;
}

double MediaPlayer::getRate(TrackType type, bool bAvg) {
  int32_t index = 0;
  if (type == TrackType::video) {
    if (index < 0 || index >= videoTracks.size()) {
      return 0.0;
    }
    return videoTracks[index]->getRate(bAvg);
  }
  if (type == TrackType::audio) {
    if (index < 0 || index >= audioTracks.size()) {
      return 0.0;
    }
    return audioTracks[index]->getRate(bAvg);
  }
  return 0.0;
}

float MediaPlayer::getLossRate(TrackType type) {
  if (type == TrackType::audio) {
    return ioAudioLoss;
  } else if (type == TrackType::video) {
    return ioVideoLoss;
  }
  return 0.0f;
}

double MediaPlayer::getFps() {
  int32_t index = 0;
  if (index < 0 || index >= videoTracks.size()) {
    return 0.0;
  }
  return videoTracks[index]->getFps();
}

void MediaPlayer::open(const char* url_) {
  log(LogLevel::info, "avox version:", AVOX_COMMIT_VERSION,
      " commit_hash:", AVOX_COMMIT_HASH, " commit_time:", AVOX_COMMIT_TIME,
      " build_time:", AVOX_BUILD_TIME);
  ioStatus.url = url_;
  auto openCmd = createCommand<MPCommandType::Open>(url_);
  mpCommands.enqueueWait(openCmd);
}

void MediaPlayer::close() {
  LOGFLF(LogLevel::info, "mpCommands size:", mpCommands.size(),
         " clear all commands");
  // 需要把之前的命令清空吗？这样可以快速关闭
  mpCommands.clear();
  // 停止
  auto stopCmd = createCommand<MPCommandType::Close>();
  mpCommands.enqueueWait(stopCmd);
}

void MediaPlayer::seek(int64_t pos) {
  seekPts = pos;
  bSeeking = true;
  // 命令还没执行, 先不启兜底超时(由 cmdSeek 生效后启动, 防止排队期间被超时误清)
  bSeekingStartMs = 0;
  bool bUpdate = false;
  // 如果队列上一个命令也是seek,则直接修改参数
  auto updateSeek = [&](const MPCommandPtr& cmd) {
    if (cmd->type == MPCommandType::Seek) {
      auto seekCmd = getCommand<MPCommandType::Seek>(cmd);
      seekCmd->setData(pos);
      bUpdate = true;
    }
  };
  mpCommands.backAction(updateSeek);
  // 上一个命令不是seek命令,则直接插入
  if (!bUpdate) {
    auto seekCmd = createCommand<MPCommandType::Seek>(pos);
    mpCommands.enqueueWait(seekCmd);
  }
}

void MediaPlayer::pause() {
  auto pauseCmd = createCommand<MPCommandType::Pause>(true);
  mpCommands.enqueueWait(pauseCmd);
}

void MediaPlayer::resume() {
  auto pauseCmd = createCommand<MPCommandType::Pause>(false);
  mpCommands.enqueueWait(pauseCmd);
}

void MediaPlayer::speed(double speed_) {
  auto speedCmd = createCommand<MPCommandType::Speed>(speed_);
  mpCommands.enqueueWait(speedCmd);
}

void MediaPlayer::complete() {
  auto completeCmd = createCommand<MPCommandType::Complete>();
  mpCommands.enqueueWait(completeCmd);
}

void MediaPlayer::onResetDecoder() {
  // 通知所有track重置解码器
  auto resetCompCmd = createCommand<MPCommandType::ResetDecodeComplete>();
  mpCommands.enqueueWait(resetCompCmd);
}

void MediaPlayer::closeSync(bool bClose) {
  if (bClose) {
    if (syncType != SyncType::none) {
      preSyncType = syncType;
    }
    // 起点不同，关闭同步
    syncType = SyncType::none;
  } else {
    // 起点相同，要求同步
    if (syncType == SyncType::none) {
      syncType = preSyncType;
    }
  }
}

void MediaPlayer::setSpeed(double speed) {
  if (ioSource) {
    ioSource->setSpeed(cspeed);
  }
  // 通知时钟，改变时钟速度
  clock->setSpeed(speed);
  for (const auto& atrack : audioTracks) {
    atrack->setSpeed(speed);
  }
  for (const auto& vtrack : videoTracks) {
    vtrack->setSpeed(speed);
  }
  // 记录速度变化
  PBPlaySpeed pb = {};
  pb.speed = speed;
  pushPB<MPPBType::SpeedChange>(mpPingback.get(), pb);
}

double MediaPlayer::getSpeed() { return cspeed; }

void MediaPlayer::renderFrame(AVTrack* track, bool bGetFrame) {
  // ready状态不进入buffering
  if (bGetFrame) {
    // 渲染拿到数据，进入playing状态
    if (state == PlayerState::ready) {
      auto playCmd = createCommand<MPCommandType::Play>();
      mpCommands.enqueueWaitBackMatch(playCmd, [](const MPCommandPtr& cmd) {
        return cmd->type != MPCommandType::Play;
      });
    }
    return;
  }
  // 检查所有IO队列是不是都没了
  // 可能视频有段数据有问题，所有包解码失败，帧队列是空
  // 但是音频最好保持继续渲染，让时间轴向前走
  bool bIoEmpty = audioStatus.queueSize == 0 && videoStatus.queueSize == 0;
  if (!bIoEmpty) {
    return;
  }
  // 如果IO给出结束信号，检测是否所有队列数据都处理了
  if (bIOComplete) {
    if (audioStatus.queueSize == 0 && videoStatus.queueSize == 0 &&
        audioStatus.frameSize == 0 && videoStatus.frameSize == 0) {
      if (state != PlayerState::completed) {
        recordRingBuffer();
      }
      auto completeCmd = createCommand<MPCommandType::Complete>();
      mpCommands.enqueueWaitBackMatch(completeCmd, [](const MPCommandPtr& cmd) {
        return cmd->type != MPCommandType::Complete;
      });
      return;
    }
  }
  if (state == PlayerState::playing) {
    // 记录播放器队列状态,这时一般是音频或是视频队列有个空了
    recordRingBuffer();
    // 记录因取不到队列数据，进入buffer
    PBMediaAction pb = {};
    pb.trackType = track->getTrackType();
    pb.mediaObject = MediaObject::track;
    pb.action = MediaAction::buffing;
    string_format(pb.msg, "clock pts ", clock->clock());
    pushPB<MPPBType::MediaAction>(mpPingback.get(), pb);
    // 如果队列上一个命令也是buffering,则不做处理
    auto bufferingCmd = createCommand<MPCommandType::Buffing>();
    mpCommands.enqueueWaitBackMatch(bufferingCmd, [](const MPCommandPtr& cmd) {
      return cmd->type != MPCommandType::Buffing;
    });
    // 记录埋点，每次buffing状态就是一次卡顿
    PBRateRecord pr = {};
    pr.trackType = track->getTrackType();
    pr.rate = track->getRateCounter().value();
    pushPB<MPPBType::RateRecord>(mpPingback.get(), pr);
  }
}

int64_t MediaPlayer::getQueueTime(TrackType type) {
  if (type == TrackType::video) {
    for (const auto& vtrack : videoTracks) {
      if (!vtrack->vaild()) {
        continue;
      }
      return vtrack->getQueueTime();
    }
  } else if (type == TrackType::audio) {
    for (const auto& atrack : audioTracks) {
      if (!atrack->vaild()) {
        continue;
      }
      return atrack->getQueueTime();
    }
  }
  return 0;
}

void MediaPlayer::recordRingBuffer() {
  for (const auto& atrack : audioTracks) {
    if (!atrack || !atrack->vaild()) {
      continue;
    }
    atrack->recordRingBuffer();
  }
  for (const auto& vtrack : videoTracks) {
    if (!vtrack || !vtrack->vaild()) {
      continue;
    }
    vtrack->recordRingBuffer();
  }
}

void MediaPlayer::pauseRender(bool pause) {
  // IO不暂停，保持下载，解码器也不暂停
  // track暂停(主要是渲染暂停,让队列里的数据不被消费)
  for (const auto& atrack : audioTracks) {
    if (atrack && atrack->vaild()) {
      atrack->pauseRender(pause);
    }
  }
  for (const auto& vtrack : videoTracks) {
    if (vtrack && vtrack->vaild()) {
      vtrack->pauseRender(pause);
    }
  }
}

void MediaPlayer::pauseDecoder(bool pause) {
  for (const auto& atrack : audioTracks) {
    if (atrack && atrack->vaild()) {
      atrack->pauseDecoder(pause);
    }
  }
  for (const auto& vtrack : videoTracks) {
    if (vtrack && vtrack->vaild()) {
      vtrack->pauseDecoder(pause);
    }
  }
}

void MediaPlayer::onRunTask() {
  mpPingback->startTask();
  while (running()) {
    MPCommandPtr cmd = nullptr;
    if (mpCommands.dequeue(cmd)) {
      switch (cmd->type) {
        case MPCommandType::Open: {
          // open,得到IO回复变成ready
          cmdOpen(getCommand<MPCommandType::Open>(cmd));
          break;
        }
        case MPCommandType::Ready: {
          // MediaPlayer::onOpen后得到流信息
          // 在打开解码线程后，进入opening状态
          cmdReady();
          break;
        }
        case MPCommandType::Play: {
          // 进入playing状态后才能进入buffing
          // 否则很容易开始ready后就进入buffing，导致等待数据
          cmdPlaying();
          break;
        }
        case MPCommandType::Pause: {
          // pause/resume
          cmdPause(getCommand<MPCommandType::Pause>(cmd));
          break;
        }
        case MPCommandType::Close: {
          // stop
          cmdClose();
          break;
        }
        case MPCommandType::Complete: {
          cmdComplete();
          break;
        }
        case MPCommandType::Seek: {
          cmdSeek(getCommand<MPCommandType::Seek>(cmd));
          break;
        }
        case MPCommandType::Buffing: {
          cmdBuffering();
          break;
        }
        case MPCommandType::Speed: {
          cmdSpeed(getCommand<MPCommandType::Speed>(cmd));
          break;
        }
        case MPCommandType::Option: {
          cmdOption(getCommand<MPCommandType::Option>(cmd));
          break;
        }
        case MPCommandType::SetWindow: {
          cmdSetWindow(getCommand<MPCommandType::SetWindow>(cmd));
          break;
        }
        case MPCommandType::ResetDecode: {
          cmdResetDecode(getCommand<MPCommandType::ResetDecode>(cmd));
          break;
        }
        case MPCommandType::ResetDecodeComplete: {
          cmdResetDecodeComplete();
          break;
        }
        case MPCommandType::SyncPts: {
          cmdSyncPts();
          break;
        }
        case MPCommandType::IFrameMode: {
          cmdIFrameMode(getCommand<MPCommandType::IFrameMode>(cmd));
          break;
        }
        default:
          break;
      }
    }
    tick();
    // 有命令以及在opening的情况下，进入就绪态，随时准备执行下一步
    sleepTask(mpCommands.size() > 0 || state == PlayerState::opening);
  }
  cmdClose();
  mpPingback->stopTask();
  LOGFLF(LogLevel::info, "media player task exit");
}

void MediaPlayer::tick() {
  if (!ioSource) {
    return;
  }
  compateIoTime();
  collectStatus();
  // 如果是buffering状态，并且上次状态是playing,避免暂停进入buffering回复播放
  if (state == PlayerState::buffering && preState == PlayerState::playing) {
    int64_t vTime = getQueueTime(TrackType::video);
    int64_t aTime = getQueueTime(TrackType::audio);
    // 存了足够数据，恢复渲染
    bool canRender = false;
    if (cspeed <= 4) {
      canRender = (vTime >= delayMs || aTime >= delayMs);
    } else {
      // 超过4倍只有I帧,有视频数据就播放,音频4倍之上不会渲染
      canRender = videoStatus.frameSize > 0;
    }
    // 速度快的话,其包会快速占满,但是超过4倍又只会有I帧
    if (canRender) {
      log(LogLevel::info, "buffering tick, vTime:", vTime, " aTime:", aTime,
          " delay ms:", delayMs, " speed:", cspeed);
      // 恢复渲染
      pauseRender(false);
      setState(PlayerState::playing);
    } else {
      // buffing时间太久了，可以认为不正常，关闭播放器
      if (bufferChecker.timeout()) {
        log(LogLevel::info, "buffering timeout, vTime:", vTime,
            " aTime:", aTime, " delay ms:", delayMs, " speed:", cspeed);
        // 一直buffing超时了，还没打开，关闭
        LOGFLF(LogLevel::warn, "buffering timeout, close player");
        cmdClose();
      }
    }
  }
  // 播放下，特别是刚连接服务器时，服务器为了秒开会缓冲一组GOP
  // 服务器会把缓存的一段GOP数据过来，如果正常播放，时延会比较大
  if (bLowLatency && bAVAlign && ioDuration <= 0 &&
      state == PlayerState::playing) {
    // 只有正常播放状态下才处理
    // 有B帧时不太好用，B帧计算getQueueTime是乱的
    // cspeed只记录用户设置的速度，不记录自动调整的速度
    // 从cmdSpeed里设置的速度才是用户设置的速度
    // 直接使用setSpeed是自动调整的速度
    if (cspeed == 1.0) {
      // 有视频的源才启用自动快播，也以视频的时延为准
      int64_t qTime = getQueueTime(TrackType::video);
      int64_t aTime = getQueueTime(TrackType::audio);
      // 队列数据太多，播放速度1.5x，减少延迟
      if (qTime > delayMs * 2 && aTime > delayMs * 2) {
        if (clock->getSpeed() != autoSpeed) {
          setSpeed(autoSpeed);
          log(LogLevel::info, "player auto speed ", autoSpeed,
              "x,video queue time:", qTime, " audio queue time:", aTime,
              " delayMs:", delayMs);
          recordRingBuffer();
        }
      } else {
        // 消费差不多了，恢复1.0x
        if ((qTime < delayMs || aTime < delayMs) && clock->getSpeed() != 1.0) {
          setSpeed(1.0);
          log(LogLevel::info, "player auto speed 1.0x,video queue time:", qTime,
              " audio queue time:", aTime, " delayMs:", delayMs);
          recordRingBuffer();
        }
      }
    }
  }
#ifdef __APPLE__
  if (state == PlayerState::playing) {
    // 切到后台后,GPU资源不能,硬解也不能用,在这直接关闭播放器算了
    if (AvoxManager::Get().getBackground()) {
      close();
    }
  }
#endif
}

void MediaPlayer::compateIoTime() {
  if (!ioSource) {
    return;
  }
  // 进度(ioPrecent)以渲染位置为准, 不用demux读位置: >4x只解I帧时, demux按
  // 磁盘速度超前读, 视频包队列可缓存上百个I帧(几十秒~几分钟内容), 用demux位置
  // 会让进度条瞬间跳到远处再慢慢爬(4x是全流进队被节流, 无此现象, 两边不一致)
  int64_t duration = ioSource->duration();
  if (duration < 0) {
    ioDuration = 0;
  } else {
    ioDuration = duration;
  }
  int64_t baseTime = ioSource->getBaseTime();
  // 如果有basetime后,返回渲染时间
  if (baseTime != AVOX_NOVALID_PTS) {
    int64_t ctime = clock->clock();
    // 如果还没开始渲染,则返回IO包时间
    if (ctime == AVOX_NOVALID_PTS) {
      ctime = ioSource->position();
    }
    // renderTime在[baseTime,baseTime+ioDuration]
    if (ctime < ioBaseTime) {
      ctime = ioBaseTime;
    }
    if (ioDuration > 0 && ctime > ioBaseTime + ioDuration) {
      ctime = ioBaseTime + ioDuration;
    }
    ioBaseTime = baseTime;
    renderTime = ctime;
    // 进度=渲染位置相对时长(与getPosition()一致): 渲染前/seek中ctime退回demux
    // 位置, 首个渲染帧后即为真实播放进度
    if (ioDuration > 0) {
      ioPrecent = (double)(renderTime - ioBaseTime) / (double)ioDuration;
      ioPrecent = std::max(0.0, std::min(ioPrecent, 1.0));
    } else {
      ioPrecent = 0.0;
    }
    // seek 后 updateSeekTime 把时钟乐观置为目标, 首个渲染帧才拉到真实落点(向后
    // seek 落点<目标)
    // 所以"追平"必须是见过落点回落(renderTime<seekPts)之后又爬回来;
    // 否则乐观时钟=目标值会提前解除 bSeeking
    if (bSeeking) {
      if (renderTime < seekPts) {
        bSeekSeenLanding = true;
      }
      int64_t endPts =
          ioDuration > 0 ? ioBaseTime + ioDuration : AVOX_NOVALID_PTS;
      if ((bSeekSeenLanding && renderTime >= seekPts) ||
          // 文件到头(seek 目标超过时长), 追平无望, 直接解除
          (endPts != AVOX_NOVALID_PTS && renderTime >= endPts) ||
          // 落点恰好等于目标、无回落(也一直没渲染)时, 超时兜底解除,
          // 防进度条永远钉在目标
          (!bSeekSeenLanding && bSeekingStartMs.load() > 0 &&
           timeStampMS() - bSeekingStartMs.load() > 3000)) {
        bSeeking = false;
        bSeekSeenLanding = false;
      }
    }
  } else {
    // 可能音频渲染了,但是basetime还没有效的情况
    // basetime以I帧来的包开始记时,音频包前面容易乱
    ioBaseTime = 0;
    renderTime = 0;
    ioPrecent = 0.0;
  }
  // 丢包统计
  ioVideoLoss = getIoLossRate(TrackType::video);
  ioAudioLoss = getIoLossRate(TrackType::audio);
}

void MediaPlayer::collectStatus() {
  // 收集状态
  for (const auto& vtrack : videoTracks) {
    if (vtrack->vaild()) {
      videoStatus = vtrack->getQueueStatus();
      break;
    }
  }
  // 检查音频的数据量与时长匹配 有些音频过了1秒,但是只有500ms数据量
  bool bAudioMatch = true;
  for (const auto& atrack : audioTracks) {
    if (atrack->vaild()) {
      audioStatus = atrack->getQueueStatus();
      bAudioMatch = atrack->matchPtsData();
      break;
    }
  }
  if (ioStatus.videoCount <= 0 || ioStatus.audioCount <= 0) {
    return;
  }
  // 4倍速之上,同步会在之前关了,这里不开启同步
  if (cspeed > 4.0) {
    return;
  }
  bAVAlign = true;
  int64_t ioDiff = std::abs(audioStatus.ioTime - videoStatus.ioTime);
  int64_t renderDiff =
      std::abs(audioStatus.renderTime - videoStatus.renderTime);
  // 音频与视频是否对齐,IO包与渲染时间都在delayMs内
  if (ioDiff > delayMs * 2) {
    bAVAlign = false;
    // 自动调整的速度，在不对齐的情况下，恢复1.0x
    if (cspeed == 1.0 && clock->getSpeed() != 1.0) {
      setSpeed(1.0);
    }
    if (syncType != SyncType::none) {
      LOGFLF(LogLevel::info, "av not align ioDiff:", ioDiff,
             " renderDiff:", renderDiff, " delayMs:", delayMs);
    }
  }
  // 如果音频数据与PTS不匹配，则不开启同步
  if (bAVAlign && !bAudioMatch) {
    bAVAlign = false;
    if (syncType != SyncType::none) {
      LOGFLF(LogLevel::warn, "audio pts span not match data");
    }
  }
  // 当同步状态变化时，记录说明
  // 第一种情况，从同步变为不同步
  // 第二种情况，从不同步变为同步
  if ((!bAVAlign && syncType != SyncType::none) ||
      (bAVAlign && syncType == SyncType::none &&
       preSyncType != SyncType::none)) {
    LOGFLF(LogLevel::info, "sync change,now sync:", bAVAlign ? "yes" : "no",
           " speed:", cspeed, " ioDiff:", ioDiff, " renderDiff:", renderDiff,
           " delayMs:", delayMs);
    closeSync(!bAVAlign);
  }
}

float MediaPlayer::getIoLossRate(TrackType type) {
  float loss = ioSource->getLossRate(type);
  // 负数其实表示无效,如果SEQ序列不对,可能超过100
  if (loss < 0 || loss >= 100) {
    loss = 0;
  }
  return loss;
}

void MediaPlayer::cmdOpen(OpenCommandPtr cmd) {
  // 如果播放器已经打开,先清空资源
  if (state != PlayerState::none && state != PlayerState::stopped) {
    // 先关闭之前对象
    cmdClose();
  }
  for (int32_t i = 0; i < AVOX_MAX_TRACK; ++i) {
    audioTracks[i]->setMediaPlayer(this);
    videoTracks[i]->setMediaPlayer(this);
  }
  setState(PlayerState::opening);
  // 记录
  PBMediaAction pb = {};
  pb.mediaObject = MediaObject::io;
  // 打开IO
  const std::string& url = cmd->getData().c_str();
  // 插件(如avox_torrent)是懒加载,首次扫描在任一注册表/工厂查询前触发,
  // 否则纯播放型App查 ioSources 永远看不到插件注册的 IO 方案
  ModuleMgr::Get().ensureStarted();
  // 磁力/torrent链接自动路由: 插件已注册时无需业务显式setIoPlan;
  // 只影响本次open, 不改动selectIO(用户对其他URL的显式选择保持不变)
  IoPlan useIO = selectIO;
  bool bTorrentUrl =
      url.rfind("magnet:", 0) == 0 ||
      (url.size() > 8 && url.compare(url.size() - 8, 8, ".torrent") == 0);
  if (bTorrentUrl && useIO != IoPlan::torrent &&
      AvoxManager::Get().ioSources.hasObjectId(IoPlan::torrent)) {
    useIO = IoPlan::torrent;
  }
  // torrent是渐进下载源: seek落到未下载区域要等片落地(秒级~十几秒),
  // 10s缓冲看门狗会误杀播放器; 业务未显式设置时默认放宽到30s
  if (useIO == IoPlan::torrent && !bufferingTimeoutUserSet) {
    bufferChecker.setDelay(30000);
  }
  bool bFind = AvoxManager::Get().ioSources.hasObjectId(useIO);
  if (!bFind) {
    pb.action = MediaAction::create;
    string_format(pb.msg, "not find ioclass:", getIoPlanStr(useIO));
    pb.result = ActionResult::fail;
    pushPB<MPPBType::MediaAction>(mpPingback.get(), pb);
    return;
  }
  // 重置
  bIOComplete = false;
  bSeeking = false;
  bSeekSeenLanding = false;
  bSeekingStartMs = 0;
  bIFrameMode = false;
  ioPrecent = 0.0f;
  ioDuration = 0;
  renderTime = 0;
  ioBaseTime = 0;
  // 查找注册并调用初始化方法生成IoParse
  const auto& ioclass = AvoxManager::Get().ioSources.initFunc(useIO);
  ioSource = std::unique_ptr<AVSource>(ioclass.initFunc());
  // 测试
  // const auto &ioclass1 =
  // AvoxManager::Get().ioSources.initFunc(IoPlan::ffmpeg); ioTest =
  // std::unique_ptr<AVSource>(ioclass1.initFunc());
  // ioTest->open(url.c_str());
  if (!ioSource) {
    pb.action = MediaAction::create;
    string_format(pb.msg, "create ioparse failed:", getIoPlanStr(useIO));
    pb.result = ActionResult::fail;
    pushPB<MPPBType::MediaAction>(mpPingback.get(), pb);
    return;
  }
  // 关联当前option
  ioSource->linkOption(this);
  // 记录IO创建
  pb.action = MediaAction::create;
  string_format(pb.msg, "create ioparse success:", getIoPlanStr(useIO));
  pb.result = ActionResult::success;
  pushPB<MPPBType::MediaAction>(mpPingback.get(), pb);
  // 数据回调到这边来处理
  ioSource->setObserver(this);
  // 如果是服务器流加速，需要在打开前设置
  if (cspeed != 1.0) {
    ioSource->setSpeed(cspeed);
  }
  bool bOpen = ioSource->open(url.c_str());
  // 绑定到muxer
  mediaMuxer->setContext(this);
  rawMuxer->setContext(this);
  // 更新状态
  PlayerState temp = bOpen ? PlayerState::opening : PlayerState::stopped;
  setState(temp);
  // 埋点记录
  pb.action = MediaAction::open;
  pb.msg = url.c_str();
  pb.result = bOpen ? ActionResult::success : ActionResult::fail;
  pushPB<MPPBType::MediaAction>(mpPingback.get(), pb);
}

void MediaPlayer::cmdReady() {
  // 更新状态,可以接受IO数据了
  setState(PlayerState::ready);
  // 通知外部
  MPOB::dispatch(&IMediaPlayerOb::onReady);
  // 记录
  PBMediaAction pb = {};
  int32_t audioSize = 0;
  int32_t vidoeSize = 0;
  // track开始先启动解码线程，在解码信息返回后再启动渲染线程
  for (const auto& atrack : audioTracks) {
    if (atrack && atrack->vaild()) {
      atrack->start();
      audioSize++;
      pb.mediaObject = MediaObject::track;
      pb.action = MediaAction::open;
      pb.trackType = TrackType::audio;
      pushPB<MPPBType::MediaAction>(mpPingback.get(), pb);
    }
  }
  for (const auto& vtrack : videoTracks) {
    if (vtrack && vtrack->vaild()) {
      vtrack->start();
      vidoeSize++;
      pb.mediaObject = MediaObject::track;
      pb.action = MediaAction::open;
      pb.trackType = TrackType::video;
      pushPB<MPPBType::MediaAction>(mpPingback.get(), pb);
    }
  }
  if (!audioSize && syncType == SyncType::audio) {
    syncType = SyncType::video;
  }
  if (!vidoeSize && syncType == SyncType::video) {
    syncType = SyncType::audio;
  }
}

void MediaPlayer::cmdPlaying() { setState(PlayerState::playing); }

void MediaPlayer::cmdClose() {
  // log(LogLevel::info, "media player close");
  // 清空原有的track
  for (const auto& atrack : audioTracks) {
    if (atrack && atrack->vaild()) {
      atrack->close();
    }
  }
  for (const auto& vtrack : videoTracks) {
    if (vtrack && vtrack->vaild()) {
      vtrack->close();
    }
  }
  // 所有线程可能都因为RingBuffer满了,调用enqueueWait阻塞
  if (ioSource) {
    // mediaMuxer现在关联的source已经是野指针了,先解除关联
    mediaMuxer->close();
    rawMuxer->close();
    ioSource->close();
    ioSource.reset();
    // 记录
    PBMediaAction pb = {};
    pb.mediaObject = MediaObject::io;
    pb.action = MediaAction::close;
    pushPB<MPPBType::MediaAction>(mpPingback.get(), pb);
  }
  // 重置时钟
  clock->reset();
  bSeeking = false;
  bSeekSeenLanding = false;
  bSeekingStartMs = 0;
  ioPrecent = 0.0f;
  ioDuration = 0;
  ioBaseTime = 0;
  renderTime = 0;
  //
  subtitleView->close();
  ioStatus.url.clear();
  setState(PlayerState::stopped);
  MPOB::dispatch(&IMediaPlayerOb::onClose);
}

void MediaPlayer::cmdPause(PauseCommandPtr cmd) {
  if (!normalState()) {
    return;
  }
  bool bPause = cmd->getData();
  clock->pause(bPause);
  if (ioSource) {
    ioSource->pause(bPause);
  }
  pauseRender(bPause);
  if (bPause) {
    setState(PlayerState::pause);
    MPOB::dispatch(&IMediaPlayerOb::onPause);
  } else {
    setState(PlayerState::playing);
    MPOB::dispatch(&IMediaPlayerOb::onResume);
  }
}

void MediaPlayer::cmdComplete() {
  // 当正常播放完成后，设置成完成状态
  setState(PlayerState::completed);
  MPOB::dispatch(&IMediaPlayerOb::onComplete);
}

void MediaPlayer::cmdSeek(SeekCommandPtr cmd) {
  if (!normalState()) {
    return;
  }
  setState(PlayerState::seek);
  // 记录当前时间
  int64_t spts = cmd->getData();
  // 先把解码器的线程暂停
  auto seekType = ioSource->seekType();
  bSeekSeenLanding = false;
  bSeekingStartMs = 0;
  if (seekType != SeekType::none) {
    // 撤背压前预通知 IO 线程打断 av_read_frame: 必须早于 pauseIOPacket ——
    // 撤背压(enqueueWait 不阻塞)的瞬间读线程即恢复读,
    // 打断标志晚设哪怕微秒快源都可能先狂奔几包; 先设标志则撤背压后 第一次
    // av_read_frame 即被截停. 正常播放时读线程阻塞在 enqueueWait(不看
    // interrupt), 不受影响
    ioSource->preSeek();
    // IO包队列暂时关闭,直接暂停ioSource可能导致RTSP seek不了
    pauseIOPacket(true);
    pauseRender(true);
    pauseDecoder(true);
    // 上面暂停只是设置标志位,在这等数据都处理到队列中
    sleepTask(false, 50);
    // 清空队列
    flush();
    bool bSeek = ioSource->seekTo(spts);
    // 成功: 保持 bSeeking(true, 由 compateIoTime 见真实位置追平目标后解除);
    // 失败: 解除, 如实报当前位置
    bSeeking = bSeek;
    bSeekSeenLanding = false;
    bSeekingStartMs = bSeek ? timeStampMS() : 0;
    if (!bSeek) {
      LOGFLF(LogLevel::warn, "failed to seek");
    }
    // 时钟定位到seek
    updateSeekTime(spts);
    pauseIOPacket(false);
    // 恢复解码线程
    pauseDecoder(false);
    pauseRender(false);
  } else {
    // 无 seek 操作(直播等不可 seek 源)
    bSeeking = false;
    bSeekSeenLanding = false;
  }
  setState(PlayerState::playing);
  // bSeeking 不再在此清除: 成功 seek 后保持到 compateIoTime 见真实位置追平目标
}

void MediaPlayer::cmdBuffering() {
  if (!normalState()) {
    return;
  }
  if (state == PlayerState::buffering) {
    return;
  }
  // 如果IO已经完成且队列为空，直接结束播放而不是进入buffering
  if (bIOComplete) {
    bool bQueueEmpty = audioStatus.queueSize == 0 &&
                       videoStatus.queueSize == 0 &&
                       audioStatus.frameSize == 0 && videoStatus.frameSize == 0;
    if (bQueueEmpty) {
      cmdComplete();
      return;
    }
  }
  // 渲染暂停,等IO与解码把数据存够
  pauseRender(true);
  setState(PlayerState::buffering);
  // 开始buffer计时
  bufferChecker.reset();
}

void MediaPlayer::cmdSpeed(SpeedCommandPtr cmd) {
  bool wasIFrameOnly = bIFrameOnlyActive.load();
  cspeed = cmd->getData();
  setSpeed(cspeed);
  // 倍速变了,重算 >4x 只I帧的生效真值(onPacket 与音频解码线程共用)
  updateIFrameOnly();
  // 速度超过4倍,如果是网络源音频可能是让视频I帧发一次
  // 而视频只有I帧,如果8倍,2秒I帧间隔,平均250ms来一个帧
  // 原来4倍前的同步逻辑失效,4倍后播放音频也无意义
  closeSync(cspeed > 4);
  // 从>4x降到<=4x(仅播放中): >4x时音频包被丢弃+demux超前读, 音频恢复位置(超前)
  // 与视频渲染位置错位, 进度条跟音频时钟会前后跳。可seek的源(本地/网络VOD)
  // 重定位到渲染位置重新对齐。可seek性以各源seekType()为准(与cmdSeek同一判据,
  // 不依赖sourceMode: ffmpeg按AVIO seekable, zlmediakit按cduration>0)
  if (state == PlayerState::playing && wasIFrameOnly && cspeed <= 4 &&
      renderTime > 0 && ioSource && ioSource->seekType() != SeekType::none) {
    log(LogLevel::info, "speed down from >4x, re-position to render time:",
        renderTime);
    seek(renderTime);
  }
  log(LogLevel::info, "media player set speed:", cspeed,
      ", need close sync:", cspeed > 4);
}

void MediaPlayer::cmdOption(OptionCommandPtr cmd) {
  // 给挂载的OptionOb对象通知变化
  // OptionData& data = cmd->getData();
  // MPOP::dispatch(&IOptionOb::onOptionChange, data.key.c_str(), data.option);
}

void MediaPlayer::cmdSetWindow(SetWindowCommandPtr cmd) {}

void MediaPlayer::cmdResetDecode(ResetDecodeCommandPtr cmd) {
  bool bHardDecode = cmd->getData();
  PBMediaAction pb = {};
  LOGFLF(LogLevel::info, bHardDecode);
  for (const auto& vtrack : videoTracks) {
    if (vtrack && vtrack->vaild()) {
      // 通知track准备重置解码器
      vtrack->setResetDecoderFlag();
      // 记录开始切换解码
      pb.mediaObject = MediaObject::decode;
      pb.action = MediaAction::reset;
      pb.trackType = TrackType::video;
      pushPB<MPPBType::MediaAction>(mpPingback.get(), pb);
    }
  }
  // 记录track的状态
  recordRingBuffer();
}

void MediaPlayer::cmdResetDecodeComplete() {
  PBMediaAction pb = {};
  for (const auto& vtrack : videoTracks) {
    if (vtrack && vtrack->vaild()) {
      vtrack->start();
      // 记录切换解码器完成
      pb.mediaObject = MediaObject::decode;
      pb.action = MediaAction::complete;
      pb.trackType = TrackType::video;
      pushPB<MPPBType::MediaAction>(mpPingback.get(), pb);
    }
  }
  // 记录track的状态
  recordRingBuffer();
}

void MediaPlayer::cmdSyncPts() {
  if (ioSource) {
    ioStatus.baseTimeMs = ioSource->getBaseTime();
    ioStatus.baseVideoMs = ioSource->getBaseTime(TrackType::video);
    ioStatus.baseAudioMs = ioSource->getBaseTime(TrackType::audio);
    bool bSynced = ioSource->checkAvSynced();
    if ((ioStatus.videoCount > 0 && ioStatus.audioCount > 0) && !bSynced) {
      closeSync(true);
      LOGFLF(LogLevel::warn, "close sync, base video ms:", ioStatus.baseVideoMs,
             " base audio ms:", ioStatus.baseAudioMs);
    } else {
      // 起点相同，要求同步
      closeSync(false);
      LOGFLF(LogLevel::info, "open sync, base video ms:", ioStatus.baseVideoMs,
             " base audio ms:", ioStatus.baseAudioMs);
    }
    pushPB<MPPBType::IOStatus>(mpPingback.get(), ioStatus);
  }
}

void MediaPlayer::cmdIFrameMode(IFrameModeCommandPtr cmd) {
  bool bNewMode = cmd->getData();
  if (bIFrameMode == bNewMode) {
    return;
  }
  bIFrameMode = bNewMode;
  // closeSync(bIFrameMode);
  log(LogLevel::info, "I-frame mode:", bIFrameMode, " speed:", cspeed);
}

bool MediaPlayer::normalState() {
  return state == PlayerState::playing || state == PlayerState::pause ||
         state == PlayerState::buffering;
}

void MediaPlayer::flush() {
  // 清空原有的track
  for (const auto& atrack : audioTracks) {
    if (atrack && atrack->vaild()) {
      atrack->flush();
    }
  }
  for (const auto& vtrack : videoTracks) {
    if (vtrack && vtrack->vaild()) {
      vtrack->flush();
    }
  }
}

void MediaPlayer::updateSeekTime(int64_t seekTime) {
  log(LogLevel::info, "updateSeekTime:", seekTime);
  for (const auto& atrack : audioTracks) {
    if (atrack && atrack->vaild()) {
      atrack->updateSeekTime(seekTime);
    }
  }
  for (const auto& vtrack : videoTracks) {
    if (vtrack && vtrack->vaild()) {
      vtrack->updateSeekTime(seekTime);
    }
  }
  clock->update(seekTime);
}

void MediaPlayer::pauseIOPacket(bool pause) {
  for (const auto& atrack : audioTracks) {
    if (atrack && atrack->vaild()) {
      atrack->getPacketQueue().setClose(pause);
    }
  }
  for (const auto& vtrack : videoTracks) {
    if (vtrack && vtrack->vaild()) {
      vtrack->getPacketQueue().setClose(pause);
    }
  }
}

}
