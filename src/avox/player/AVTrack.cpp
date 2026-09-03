#include "AVTrack.hpp"

#include "../module/RunTask.hpp"
#include "MPPingback.hpp"
#include "MediaPlayer.hpp"

namespace avox {

AVTrack::AVTrack() {
  clock = std::make_unique<Clock>();
  packetQueue.setMaxSize(100);
  bVaild = false;
}

AVTrack::~AVTrack() {}

void AVTrack::onInitDesc() {
  // 设置track有效，相应于创建
  bVaild = true;
  bResetBase = true;
  bDecodeEnd = false;
  // 重置状态
  queueStatus = {};
  queueStatus.trackType = trackType;
  packetQueue.setClose(false);
  // 接受mediaplay选项变化
  if (mediaPlayer) {
    int64_t duration = mediaPlayer->getDuration();
    if (duration > 0) {
      // 点播/下载源: 假定1个包是40ms,1000个包是40s,够把一个片断下载完
      packetQueue.setMaxSize(1000);
    } else {
      // 直播流，太大延迟会比较大，太小网络波动会卡顿
      // 设大点还为了处理音频与视频PTS相差在2秒左右的情况
      // 太大会自动关闭同步，太小会怕队列占满也不能首尾相连
      // 设定200个，如果间隔是40MS，大约是8秒，可以容忍8秒间隔
      packetQueue.setMaxSize(200);
    }
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
  packetQueue.enqueueWait<AvoxPacket>(data, copyBuf);
  // 显示小bit
  rateCounter.record(data.data.size * 8);
  // 记录IO线程输入PTS
  queueStatus.ioTime = data.pts;
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
  // 记录解码器输入PTS
  queueStatus.decodeInTime = packet->pts;
  queueStatus.queueSize = packetQueue.size();
}

void AVTrack::renderFirst() {
  // 当前PTS基准是否需要改变，比如flush,close后需要改变
  if (bResetBase) {
    renderTime = timeStampMS();
    resetPts();
    bResetBase = false;
  }
}

void AVTrack::updateClock(int64_t pts) {
  // LOGFLF(LogLevel::info, "pts:", pts, " type:", getTrackTypeStr(trackType));
  // 更新自身时钟
  clock->update(pts);
  SyncType syncType = mediaPlayer->getSyncType();
  // 如果没同步,用视频的渲染值给播放器
  if ((syncType == SyncType::video && trackType == TrackType::video) ||
      (syncType == SyncType::none && trackType == TrackType::video) ||
      (syncType == SyncType::audio && trackType == TrackType::audio)) {
    // 同步给播放器时钟
    mediaPlayer->getExtClock()->sync(clock.get());
  }
  // 字幕与音频同步，字幕时钟跟随音频时钟走
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

const char* getDefaultDecoderName(VCodecId codecId, bool bHard) {
  if (codecId == VCodecId::h264) {
// 注意不同编码器，sps/vps解码有些可以，有些不行
#ifdef __ANDROID__
    // AVOX_ANDROID_H264_DECODER h264
    return bHard ? AVOX_ANDROID_H264_DECODER : AVOX_FF_H264_DECODER;
#elif __APPLE__
    // h264_qsv h264_vulkan h264 libx264
    return bHard ? AVOX_IOS_H264_DECODER : AVOX_FF_H264_DECODER;
#else
    // h264_qsv h264_vulkan h264 libx264
    return bHard ? AVOX_FFDX11_H264_DECODER : AVOX_FF_H264_DECODER;
#endif
  } else if (codecId == VCodecId::h265) {
    // hevc_qsv hevc_vulkan hevc libx265
#ifdef __ANDROID__
    // AVOX_ANDROID_H264_DECODER hevc
    return bHard ? AVOX_ANDROID_H265_DECODER : AVOX_FF_H265_DECODER;
#elif __APPLE__
    // hevc_qsv hevc_vulkan hevc libx264
    return bHard ? AVOX_IOS_H265_DECODER : AVOX_FF_H265_DECODER;
#else
    // hevc_qsv hevc_vulkan hevc libx265 AVOX_FFVULKAN_H265_DECODER
    // AVOX_FFDX11_H265_DECODER
    return bHard ? AVOX_FFDX11_H265_DECODER : AVOX_FF_H265_DECODER;
#endif
  }
  return AVOX_FF_H264_DECODER;
}

template class TAVTrack<VideoFramePtr>;
template class TAVTrack<AudioFramePtr>;

}
