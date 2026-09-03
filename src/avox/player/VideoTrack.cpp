#include "VideoTrack.hpp"

#include "../module/AvoxManager.hpp"
#include "../video/VideoDecoder.hpp"
#include "MediaPlayer.hpp"

namespace avox {

VideoTrack::VideoTrack() {
  // android如果选择硬解码,经测试，大于10容易花屏
  // 因为视频帧资源大，队列长度是有限制的，这边的长度超过解码队列
  // 取对应的帧索引可能已经被释放了而出现问题
  frameQueue.setMaxSize(10);
  trackType = TrackType::video;
  decodeTask = std::make_unique<VDecoderTask>();
  windowRender = std::make_unique<WindowRender>();
}

VideoTrack::~VideoTrack() {}

void VideoTrack::setTrackDesc(const VTrackDesc& trackDesc) {
  setTrackId(trackDesc.trackId);
  codecId = trackDesc.codecId;
  srcDesc = trackDesc.desc;
  // 如果没有fps，默认25
  if (srcDesc.fps == 0) {
    srcDesc.fps = 25;
  }
  if (codecId == VCodecId::none) {
    LOGFLF(LogLevel::warn, "unsupported codec ", (int32_t)codecId);
    return;
  }
  onInitDesc();
  frameQueue.setClose(false);
}

void VideoTrack::start() {
  windowRender->setFrameSource(this);
  windowRender->start();
  bool bInit = decodeTask->start(this);
  bHardDecode = decodeTask->hardDecode();
  if (!bInit) {
    log(LogLevel::warn, "decodeTask start failed");
    return;
  }
  if (mediaPlayer) {
    SubtitleView* subtitleView = mediaPlayer->getSubtitleView();
    if (subtitleView) {
      subtitleView->setWindowRender(windowRender.get());
    }
  }
  log(LogLevel::info, "decode and render start success");
}

void VideoTrack::onVideoDesc() {
  // 设置图像格式
  ImageFormat imageFormat = {};
  imageFormat.width = srcDesc.width;
  imageFormat.height = srcDesc.height;
  // 记录视频编码信息
  const DecoderParams& dparams = decodeTask->getDecoder()->getDecoderParams();
  // 优先使用解码器配置
  if (dparams.width > 0 && dparams.height > 0) {
    imageFormat.width = dparams.width;
    imageFormat.height = dparams.height;
  }
  windowRender->setFPS(dparams.fps);
  dropDuration = (int32_t)(1000.0 / windowRender->getFPS()) * 2;
  // 记录track里的渲染器创建
  PBMediaAction pb = {};
  pb.mediaObject = MediaObject::render;
  pb.trackType = trackType;
  pb.action = MediaAction::create;
  pushPB<MPPBType::MediaAction>(mpPingback, pb);
}

void VideoTrack::onPacket(PacketBufPtr packet) { pullPacket(packet); }

void VideoTrack::onDecode(const YUVFrame& frame) {
  // 压入Frame队列,这里队列如果满了，会一直阻塞解码线程
  // Packet不能覆盖，但是帧应该是覆盖过去
  // 直播应该是覆盖，而本地播放应该是等待？
  // 因为直播过来IO与播放应该是相同的，而本地IO会很快
  frameQueue.enqueueWait<YUVFrame>(frame, copyBufHost);
  // frameQueue.enqueue<YUVFrame>(frame, copyBufHost, true);
  logDecode(frame.pts, frame.keyFrame);
}

void VideoTrack::onDecodeGpu(const GpuFrame& frame) {
  frameQueue.enqueueWait<GpuFrame>(frame, copyBufGpu);
  // frameQueue.enqueue<GpuFrame>(frame, copyBufGpu, true);
  logDecode(frame.pts, frame.keyFrame);
}

void VideoTrack::onVideoComplete() {
  bDecodeEnd = true;
  // 记录解码结束
  PBMediaAction pb = {};
  pb.mediaObject = MediaObject::decode;
  pb.trackType = trackType;
  pb.action = MediaAction::complete;
  pushPB<MPPBType::MediaAction>(mpPingback, pb);
}

double VideoTrack::getFps() { return fpsCounter.value(); }

void VideoTrack::onWinUpdate() {
  // 处理帧数据，这个Frame不好提取出来
  // 因为frame如果不在队列锁里，可能异步访问时有问题
  // 比如外面在清除队列，这时引发解码器释放队列里GPU数据
  // 然后当前又在窗口线程访问GPU数据，就会出问题
  // 但是这个锁导致别的线程访问队列锁信息时，同步代价大
  auto frameAction = [&](const VideoFramePtr& frame) {
    windowRender->render(*frame);
  };
  // 确定是否需要刷新画画
  SyncResult syncResult = syncVideo();
  // LOGFLF(LogLevel::info, "syncType ", getSyncResultStr(syncResult));
  if (syncResult == SyncResult::none || syncResult == SyncResult::slow) {
    //  FrameQueue里的线程锁下执行
    bool bGet = getFrameQueue().dequeueAction(frameAction);
    if (bGet && mediaPlayer) {
      RawMuxer* rawMuxer = mediaPlayer->getRawMuxer();
      if (rawMuxer && rawMuxer->getState() == RecorderState::recording) {
        YUVFrame yframe = {};
        if (windowRender->getCpuFrame(yframe)) {
          rawMuxer->pushFrame(yframe);
        }
      }
    }
    onFrameResult(bGet);
  }
  windowRender->setSyncResult(syncResult);
  // 同步二者的速度
  if (windowRender->getSpeed() != clock->getSpeed()) {
    windowRender->setSpeed(clock->getSpeed());
  }
}

WindowRender* VideoTrack::getSurfaceRender() { return windowRender.get(); }

void VideoTrack::setResetDecoderFlag() {
  if (decodeTask) {
    decodeTask->setResetFlag();
  }
}

void VideoTrack::onResetDecoder() {
  // 不同解码器，帧队列可能不同，清空原队列
  frameQueue.clear();
  clock->reset();
  // 当前在解码器线程中，转到播放器线程中重开解码器
  if (mediaPlayer) {
    mediaPlayer->onResetDecoder();
  }
}

// compute_target_delay
int64_t VideoTrack::computeDelay(int64_t delay) {
  if (!mediaPlayer) {
    return delay;
  }
  if (mediaPlayer->getSyncType() == SyncType::none) {
    return delay;
  }
  Clock* mainClock = mediaPlayer->getMainClock();
  // 如果不是主时钟，需要与主时钟比较差异
  if (clock.get() != mainClock) {
    int64_t sClock = clock->clock();
    mClock = mainClock->clock();
    if (sClock == AVOX_NOVALID_PTS || mClock == AVOX_NOVALID_PTS) {
      return delay;
    }
    // 视频与主时钟的差值，正表明视频快，负表明视频慢
    int64_t diff = sClock - mClock;
    int64_t threshold = std::max(AVOX_SYNC_THRESHOLD_MIN,
                                 std::min(AVOX_SYNC_THRESHOLD_MAX, delay));
    if (diff <= -threshold) {
      // 视频慢了
      delay = std::max((int64_t)0, delay + diff);
    } else if (diff >= threshold && delay > AVOX_SYNC_THRESHOLD_MAX) {
      // 视频快了，且与上帧间隔大
      delay = delay + diff;
    } else if (diff >= threshold) {
      // ​视频过快，但是与上帧间隔小，丢弃
      delay = 2 * delay;
    }
  }
  return delay;
}

// ffplay video_refresh
// -1 视频帧慢了,丢弃,无效帧 0 同步帧 1 视频帧快了 2 队列没帧了
SyncResult VideoTrack::syncVideo() {
  // 取队列的最前面frame的数据，但是不弹出
  VideoFramePtr frame = nullptr;
  bool bGet = frameQueue.peek(frame);
  if (!bGet) {
    return SyncResult::nodata;
  }
  double speed = clock->getSpeed();
  bool bIFrameMode = false;
  if (mediaPlayer) {
    bIFrameMode = mediaPlayer->getIFrameMode();
  }
  int64_t pts = frame->pts;
  int64_t tPts = prePts;
  // 记录自身基准，frameTimer
  renderFirst();
  // frame没有使用,故不更新,保持正常的duration
  if (pts != prePts) {
    // 更新pts与duration
    updatePts(pts);
  }
  int64_t sduration = duration;
  // 需要调整duration,让简隔时间根据speed变化
  sduration = (int64_t)((double)duration / speed);
  int64_t now = timeStampMS();
  // 时间戳跳变,如何处理？先记录
  // I帧模式下间隔大是正常的,不触发跳变处理
  if (!bIFrameMode && (sduration > AVOX_NOSYNC_THRESHOLD || sduration < 0)) {
    log(LogLevel::warn, "video sync jump:", duration, " now pts:", pts,
        " pre pts:", pts - duration, " main pts:", mClock, " speed:", speed);
    renderTime = now;
    duration = 0;
    sduration = 0;
  }
  // delay<duration视频慢了 delay>duration视频快了
  int64_t delay = computeDelay(sduration);
  // I帧模式不用音频
  if (bIFrameMode) {
    delay = sduration;
  }
  // 还不到渲染时间，让窗口等待
  if (renderTime + delay > now) {
    // LOGFLF(LogLevel::warn, "wait render:", renderTime + delay, " now:", now,
    //        " delay:", delay, " duration:", duration, " pts:", pts,
    //        " pre pts:", tPts);
    return SyncResult::quick;
  }
  // 下次更新时间,renderTime应该一直now的时间差不多
  // now受刷新线程影响，如果刷新线程每次比duration慢1-2ms
  // 如果是20FPS,大约20多帧后，renderTime比now小一帧时间，引发丢帧
  renderTime = renderTime + delay;
  // renderTime应该与now差不多
  if (delay > 0 && now - renderTime > AVOX_SYNC_THRESHOLD_MAX) {
    // 高倍率丢帧相对频繁，导致renderTime一直追不上now，不打印
    // log(LogLevel::warn, "reset render time:", renderTime, " now:", now,
    //     " delay:", delay, " duration:", duration, " pts:", pts,
    //     " pre pts:", tPts);
    renderTime = now;
  }
  // 更新时钟并同步给播放器
  updateClock(pts);
  // log(LogLevel::info, "main pts:", mClock, " now:", now,
  //     " render time:", renderTime, " speed:", speed,
  //     " speed duration:", sduration, " video pts:", pts, " pre pts:", tPts,
  //     " queue size:", packetQueue.size(), " frame size:", frameQueue.size());
  // 渲染时间慢了，需要丢帧
  // I帧模式下帧少,不丢帧
  if (!bIFrameMode && now > renderTime + sduration * 1.5 && sduration > 0) {
    int64_t dropPts = 0;
    // GPU数据丢弃需要通知frame好做后续处理
    frameQueue.pop([&](const VideoFramePtr& frame) {
      dropPts = frame->pts;
      frame->release();
    });
    // log(LogLevel::warn, "drop frame pts:", dropPts, " main pts:", mClock,
    //     " now:", now, " render time:", renderTime, " speed:", speed,
    //     " speed duration:", sduration, " pts:", pts, " pre pts:", tPts,
    //     " queue size:", packetQueue.size(),
    //     " frame size:", frameQueue.size());
    return SyncResult::slow;
  }
  // 视频慢了，让窗口快速刷新，不要sleep了
  if (delay == 0) {
    // return SyncResult::slow;
  }
  return SyncResult::none;
}

void VideoTrack::pauseRender(bool pause) {
  if (windowRender) {
    windowRender->pause(pause);
  }
  clock->pause(pause);
}

void VideoTrack::pauseDecoder(bool pause) {
  if (decodeTask) {
    if (pause) {
      decodeTask->pauseTask();
    } else {
      decodeTask->resumeTask();
    }
    // 记录解码暂停
    PBMediaAction pb = {};
    pb.mediaObject = MediaObject::decode;
    pb.trackType = trackType;
    pb.action = pause ? MediaAction::pause : MediaAction::resume;
    pushPB<MPPBType::MediaAction>(mpPingback, pb);
  }
}

void VideoTrack::flush() {
  if (decodeTask) {
    decodeTask->flush();
  }
  // 重置队列
  packetQueue.clear();
  frameQueue.clear();
  clock->reset();
  bResetBase = true;
  // 记录flush
  PBMediaAction pb = {};
  pb.mediaObject = MediaObject::track;
  pb.trackType = trackType;
  pb.action = MediaAction::flush;
  pushPB<MPPBType::MediaAction>(mpPingback, pb);
}

void VideoTrack::updateSeekTime(int64_t seekTime) { clock->update(seekTime); }

void VideoTrack::close() {
  packetQueue.setClose(true);
  // 先通知frameQueue不可用，保证decodeTask顺利关闭
  frameQueue.setClose(true);
  // 调整下，android下，先关闭EglVideoRender，再关闭AndVDecoder
  // 否则可能EglVideoRender可能还在渲染纹理frame->release(true)时
  // 而AndVDecoder已经释放了Mediacodec，导致崩溃
  if (windowRender) {
    windowRender->stop();
    windowRender->setFrameSource(nullptr);
    // 记录渲染关闭
    PBMediaAction pb = {};
    pb.mediaObject = MediaObject::render;
    pb.trackType = trackType;
    pb.action = MediaAction::close;
    pushPB<MPPBType::MediaAction>(mpPingback, pb);
  }
  if (decodeTask) {
    decodeTask->close();
    // 队列里的decoder置空，现在是野指针
    frameQueue.action([&](VideoFramePtr& frame) {
      if (frame->buffer) {
        frame->buffer->reset();
      }
    });
    // 记录解码器关闭
    PBMediaAction pb = {};
    pb.mediaObject = MediaObject::decode;
    pb.trackType = trackType;
    pb.action = MediaAction::close;
    pushPB<MPPBType::MediaAction>(mpPingback, pb);
  }
  // 清空队列
  packetQueue.clear();
  frameQueue.clear();
  onUninitDesc();
}

}
