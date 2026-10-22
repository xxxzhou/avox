#include "AVTrack.hpp"

#include "../module/RunTask.hpp"
#include "../source/PacketBufPool.hpp"
#include "MPPingback.hpp"
#include "MediaPlayer.hpp"

namespace avox {

AVTrack::AVTrack() {
  clock = std::make_unique<Clock>();
  packetQueue.setMaxSize(100);
  // 池随轨构造、终生不替换(防切音轨时跨线程替换 shared_ptr 的 UB), 换流只 reset
  packetPool = std::make_shared<PacketBufPool>();
  bVaild = false;
}

AVTrack::~AVTrack() {}

void AVTrack::onInitDesc() {
  // 设置track有效，相应于创建
  bVaild = true;
  bResetBase = true;
  bDecodeEnd = false;
  // 池随轨构造、终生不替换; 换流/切音轨只清囤积(在飞块由旧包死亡自然回流)
  packetPool->reset();
  // 重置状态
  queueStatus = {};
  queueStatus.trackType = trackType;
  packetQueue.setClose(false);
  // 接受mediaplay选项变化
  if (mediaPlayer) {
    // 点播/直播统一200(≈8秒@40ms): 队列深度只决定demux领先量与断流容忍,
    // 下载完成时刻≈播放时长-领先量, 加深不加速下载只费内存; 容忍A/V PTS相差约2秒
    packetQueue.setMaxSize(200);
  }
  // 记录Track有效
  PBMediaAction pb = {};
  pb.mediaObject = MediaObject::track;
  pb.trackType = trackType;
  pb.action = MediaAction::create;
  pushPB<MPPBType::MediaAction>(mpPingback, pb);
}

void AVTrack::onUninitDesc() {
  if (bVaild) {
    // 记录track关闭
    PBMediaAction pb = {};
    pb.mediaObject = MediaObject::track;
    pb.trackType = trackType;
    pb.action = MediaAction::close;
    pushPB<MPPBType::MediaAction>(mpPingback, pb);
  }
  bVaild = false;
  bResetBase = true;
  // 池验收统计: 一轨两行, 只在确实跑过包时打(判"池在不在生效"用)
  if (packetPool && packetPool->statAcquires() > 0) {
    log(LogLevel::info, "packet pool stats: acquires:", packetPool->statAcquires(),
        " hit:", packetPool->statHits(), " new:", packetPool->statBlockNews(),
        " slotReuse:", packetPool->statSlotReuses(),
        " slotNew:", packetPool->statSlotNews(),
        " objReuse:", packetPool->statObjHits(),
        " objNew:", packetPool->statObjNews(),
        " peakKB:", packetPool->statPeakBytes() / 1024);
    // 分档命中/新建(末列是超 8M 未入池), 8K/32K/128K/512K/2M/8M
    std::string byClass;
    for (int32_t i = 0; i < PacketBufPool::classCount(); ++i) {
      char item[48] = {};
      snprintf(item, sizeof(item), " %lld/%lld",
               (long long)packetPool->statClassHit(i),
               (long long)packetPool->statClassNew(i));
      byClass += item;
    }
    log(LogLevel::info, "packet pool by class(hit/new):", byClass);
  }
  // 统计打完后清囤积: stop 后每轨空闲块(双轨最坏 64MB)不必压到下次 open
  if (packetPool) {
    packetPool->reset();
  }
  // 重置时钟
  clock->reset();
}

void AVTrack::onDecodeError(DecodeResult error) {
  mediaPlayer->onDecodeError(this, error);
  // 后续完善，切换别的解码器
}

double AVTrack::getSpeed() {
  if (clock) {
    return clock->getSpeed();
  }
  return 1.0;
}

double AVTrack::getRate(bool bAvg) {
  if (bAvg) {
    int64_t span = rateCounter.getTotalSpan();
    if (span <= 0) return 0.0;
    // 前面*1000转成毫秒,后面*1000转成kb,1000抵消
    return rateCounter.getTotalValue() / span;
  } else {
    // Kb/s
    return rateCounter.value() / 1000.0;
  }
}

void AVTrack::pushPacket(const AvoxPacket& data) {
  onPrePushPacket();
  // 如果满了,阻塞等待,vaildFunc是当前线程如果停了，就不要堵塞了
  // 走包缓冲池: 稳态下按尺寸档复用, 不再每包 malloc
  const auto& pool = packetPool;
  packetQueue.enqueueWait<AvoxPacket>(
      data, [&pool](PacketBufPtr& pack, const AvoxPacket& pkt) {
        copyBuf(pack, pkt, pool);
      });
  // 显示小bit
  rateCounter.record(data.data.size * 8);
  // 记录IO线程输入PTS; 无效pts透传时保持上一个有效值, 供对齐检查
  if (data.pts != AVOX_NOVALID_PTS) {
    queueStatus.ioTime = data.pts;
  }
  queueStatus.queueSize = packetQueue.size();
  // 记录码率
  if (mpPingback->canLogBitrate() && rateCounter.bTrigger()) {
    PBRateRecord pr = {};
    pr.trackType = trackType;
    pr.rate = rateCounter.value();
    pushPB<MPPBType::RateRecord>(mpPingback, pr);
  }
}

void AVTrack::pullPacket(PacketBufPtr packet) {
  // 记录解码器输入PTS; 无效pts不覆盖, 保持最后一个有效值
  if (packet->pts != AVOX_NOVALID_PTS) {
    queueStatus.decodeInTime = packet->pts;
  }
  queueStatus.queueSize = packetQueue.size();
}

void AVTrack::renderFirst() {
  // 当前PTS基准是否需要改变，比如flush,close后需要改变
  if (bResetBase) {
    renderTime = timeStampMS();
    // 起播/seek后首帧起 1s 保护窗: 渲染链首拍惰性初始化实测拖 100ms+ 且
    // 簇尾可到 0.56s(DX11设备/CS编译/Vk建图分摊在前几拍), 窗内丢帧门禁用,
    // 过期帧顺序上屏不跳变
    warmupUntilMs = renderTime + 1000;
    resetPts();
    bResetBase = false;
  }
}

bool AVTrack::hasVaildVideoTrack() {
  if (!mediaPlayer) {
    return false;
  }
  for (const auto& vtrack : mediaPlayer->getVideoTracks()) {
    if (vtrack && vtrack->vaild()) {
      return true;
    }
  }
  return false;
}

void AVTrack::updateClock(int64_t pts) {
  // LOGFLF(LogLevel::info, "pts:", pts, " type:", getTrackTypeStr(trackType));
  // 更新自身时钟
  clock->update(pts);
  SyncType syncType = mediaPlayer->getSyncType();
  // 如果没同步,用视频的渲染值给播放器
  if ((syncType == SyncType::video && trackType == TrackType::video) ||
      (syncType == SyncType::none && trackType == TrackType::video) ||
      (syncType == SyncType::audio && trackType == TrackType::audio) ||
      // 纯音频源(sync=no): 音频渲染也要喂主时钟, 否则时钟一直无效,
      // getPosition 退回 demux 位置(快源冲到EOF后冻结在文件尾, 进度跳到结尾)
      (syncType == SyncType::none && trackType == TrackType::audio &&
       !hasVaildVideoTrack())) {
    // 同步给播放器时钟
    mediaPlayer->getExtClock()->sync(clock.get());
  }
  // 字幕统一视图单时钟(合并计划 D4): 文本与轨通道两路共用, 渲染线程按它取 pts
  SubtitleView* subtitleView = mediaPlayer->getSubtitleView();
  if (subtitleView) {
    subtitleView->getClock()->sync(clock.get());
  }
  if (mpPingback->canLogPts()) {
    PBPtsTick pp = {};
    pp.pts = pts;
    pp.trackType = trackType;
    pushPB<MPPBType::PtsTick>(mpPingback, pp);
  }
}

void AVTrack::setSpeed(double speed) {
  clock->setSpeed(speed);
  onSpeed();
}

template <typename T>
int64_t TAVTrack<T>::getFrameQueueTime() {
  auto getTime = [](const T& begin, const T& end) {
    return end->pts - begin->pts;
  };
  return frameQueue.template counter<int64_t>(getTime);
}

template <typename T>
int64_t TAVTrack<T>::getQueueTime() {
  if (queueStatus.queueSize == 0 && queueStatus.frameSize == 0) {
    return 0;
  }
  int64_t cacheTime = 0;
  // 先算IO包里的时长
  if (queueStatus.queueSize > 0) {
    // ioTime为NOVALID，可能IO线程没正常启动，IO线程没有插入包
    // decodeInTime为NOVALID，可能解码线程可能没有打开
    if (queueStatus.ioTime != AVOX_NOVALID_PTS &&
        queueStatus.decodeInTime != AVOX_NOVALID_PTS) {
      cacheTime = queueStatus.ioTime - queueStatus.decodeInTime;
    } else if (queueStatus.decodeInTime == AVOX_NOVALID_PTS &&
               queueStatus.ioTime != AVOX_NOVALID_PTS) {
      cacheTime = queueStatus.ioTime;
    }
    // 可能重设了IO基准时间,包时间中间有段重置头了
    if (cacheTime < 0) {
      cacheTime = -cacheTime;
    }
  }
  // 再算帧队列里的时长
  if (queueStatus.frameSize > 0) {
    int32_t frameTime = 0;
    // renderTime为NOVALID，渲染线程没启动
    // decodeOutTime为NOVALID，解码线程或是不成功
    if (queueStatus.renderTime != AVOX_NOVALID_PTS &&
        queueStatus.decodeOutTime != AVOX_NOVALID_PTS) {
      frameTime = queueStatus.decodeOutTime - queueStatus.renderTime;
    }
    if (frameTime < 0) {
      frameTime = -frameTime;
    }
    cacheTime += frameTime;
  }
  return cacheTime;
}

template <typename T>
void TAVTrack<T>::logDecode(int64_t curPts, int32_t bKeyFrame) {
  // 记录解码器输出PTS
  queueStatus.decodeOutTime = curPts;
  queueStatus.frameSize = frameQueue.size();
  if (mediaPlayer && mediaPlayer->logDFrame()) {
    if (trackType == TrackType::audio) {
      log(LogLevel::info, "---", getTrackTypeStr(trackType),
          " decoder frame pts:", curPts, " frame size:", bKeyFrame,
          " frame size:", frameQueue.size(),
          " packet size:", packetQueue.size());
    } else if (trackType == TrackType::video) {
      log(LogLevel::info, "---", getTrackTypeStr(trackType),
          " decoder frame pts:", curPts, " keyframe:", bKeyFrame,
          " frame size:", frameQueue.size(),
          " packet size:", packetQueue.size());
    }
  }
}

template <typename T>
void TAVTrack<T>::onFrameResult(bool bGet) {
  if (bGet) {
    // 记录渲染PTS
    queueStatus.renderTime = clock->clock();
    queueStatus.frameSize = frameQueue.size();
    if (bDecodeEnd && packetQueue.empty() && frameQueue.empty()) {
      mediaPlayer->complete();
    }
    fpsCounter.record();
    if (mediaPlayer->logRFrame()) {
      log(LogLevel::info, "---", getTrackTypeStr(trackType),
          " render clock:", clock->clock(), " frame size:", frameQueue.size(),
          " packet size:", packetQueue.size());
    }
  }
  mediaPlayer->renderFrame(this, bGet);
}

template <typename T>
void TAVTrack<T>::recordRingBuffer() {
  queueStatus.queueSize = packetQueue.size();
  queueStatus.frameSize = frameQueue.size();
  // 记录track里packet/frame队列状态
  pushPB<MPPBType::QueueStatus>(mpPingback, queueStatus);
}

template <typename T>
void TAVTrack<T>::onPrePushPacket() {
  // IO线程不能一直被阻塞，否则可能会连接服务器的socket线程关闭
  // 检查是否队列是满的,然后又一直没有使用packet/frame队列
  // if (packetQueue.full() && frameQueue.size()) {
  //   LOGFLF(LogLevel::warn, "packet and frame full,io wait");
  // }
}

template class TAVTrack<VideoFramePtr>;
template class TAVTrack<AudioFramePtr>;

}
