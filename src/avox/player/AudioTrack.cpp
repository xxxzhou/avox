#include "AudioTrack.hpp"

#include "../module/AvoxManager.hpp"
#include "MediaPlayer.hpp"
namespace avox {

AudioTrack::AudioTrack() {
  // 音频帧多存些，而视频帧占用资源多.
  // 相应的解码器队列也有限制，故不大
  frameQueue.setMaxSize(100);
  trackType = TrackType::audio;
  decodeTask = std::make_unique<ADecoderTask>();
  renderTask = std::make_unique<ARenderTask>();
}

void AudioTrack::setTrackDesc(const ATrackDesc& trackDesc) {
  setTrackId(trackDesc.trackId);
  srcDesc = trackDesc.desc;
  codecId = trackDesc.codecId;
  decodeDesc = trackDesc.desc;
  if (codecId == ACodecId::none) {
    LOGFLF(LogLevel::warn, "unsupported codec ", (int32_t)codecId);
    return;
  }
  onInitDesc();
  frameQueue.setClose(false);
}

IAudioRender* AudioTrack::getAudioRender() {
  return renderTask->getAudioRender();
}

void AudioTrack::start() {
  bool bInit = decodeTask->start(this);
  if (!bInit) {
    log(LogLevel::warn, "decodeTask start failed");
    return;
  }
  log(LogLevel::info, "decodeTask start success");
}

void AudioTrack::onAudioDesc() {
  // 这时解码器的输出已经确定
  decodeDesc = decodeTask->getDecoder()->getOutDesc();
  if (mediaPlayer) {
    SubtitleView* subtitleView = mediaPlayer->getSubtitleView();
    if (subtitleView) {
      subtitleView->setAudioDesc(decodeDesc);
    }
  }
  // 计算bufferMs的数据量
  frameSize = getAudioFrameSize(decodeDesc, frameMs);
  assert(frameSize > 0);
  curFrame.setSize(frameSize);
  curFrame.setPts(AVOX_NOVALID_PTS);
  // 渲染器
  renderTask->start(this);
}

void AudioTrack::onSpeed() { renderTask->speed(clock->getSpeed()); }

void AudioTrack::onPacket(PacketBufPtr packet) { pullPacket(packet); }

void AudioTrack::onDecode(const AvoxAFrame& frame) {
  assert(frameSize > 0 && curFrame.getSize() == frameSize);
  uint8_t* data = frame.buffer.data;
  int32_t size = frame.buffer.size;
  // frame的开始pts
  int64_t spts = frame.pts;
  // 检查音频数据量与PTS间隔时长
  if (!startCheck || spts < checkPts ||
      std::abs(curFrame.getPts() - spts) > checkInterval / 2) {
    startCheck = true;
    checkPts = spts;
    checkDataSize = 0;
  }
  if (startCheck) {
    checkDataSize += size;
    if (spts - checkPts > checkInterval) {
      int32_t intervalMs = checkDataSize * frameMs / frameSize;
      if (intervalMs > checkInterval * 1.5 ||
          intervalMs < checkInterval * 0.7) {
        if (!bCheckfail) {
          LOGFLF(LogLevel::info,
                 "audio pts span no match data ms,data:", intervalMs,
                 " pts span:", checkInterval);
        }
        bCheckfail = true;
      } else {
        if (bCheckfail) {
          LOGFLF(LogLevel::info, "audio pts span match data ms");
        }
        bCheckfail = false;
      }
      startCheck = false;
    }
  }
  // log(LogLevel::info, "decode pts:", spts);
  // 语音识别
  if (mediaPlayer) {
    SubtitleView* subtitleView = mediaPlayer->getSubtitleView();
    if (subtitleView) {
      subtitleView->inputSpeech(frame.buffer, frame.pts);
    }
  }
  logDecode(spts, size);
  // 第一次进来，初始化当前包的时间戳
  if (curFrame.getPts() == AVOX_NOVALID_PTS) {
    curFrame.setPts(spts);
  }
  // 写入数据,数据每次组成固定bufferMs的长度bufferSize
  while (size > 0) {
    // 当前BUFFER剩余空间
    int32_t spaceleft = curFrame.spaceLeft();
    if (size >= spaceleft) {
      // 写入当前BUFFER并写满
      curFrame.writeBytes(data, spaceleft);
      // 压入Frame队列
      frameQueue.enqueueWait<AudioFrame>(curFrame, copyAudioFrame);
      // 开始新的curFrame
      curFrame.clear();
      // 下一帧的开始时间
      spts += getAudioFrameMs(decodeDesc, spaceleft);
      curFrame.setPts(spts);
      // 剩余数据继续
      data += spaceleft;
      size -= spaceleft;
    } else {
      // 写入当前BUFFER未满
      curFrame.writeBytes(data, size);
      size = 0;
    }
  }
}

void AudioTrack::muxerFrame(const AudioFrame& aframe) {
  if (mediaPlayer) {
    RawMuxer* rawMuxer = mediaPlayer->getRawMuxer();
    if (rawMuxer && rawMuxer->getState() == RecorderState::recording) {
      AvoxAFrame frame = {};
      frame.pts = aframe.getPts();
      frame.buffer.data = (uint8_t*)aframe.point();
      frame.buffer.size = aframe.getSize();
      rawMuxer->pushFrame(frame);
    }
  }
}

void AudioTrack::onAudioComplete() {
  bDecodeEnd = true;
  // 记录解码完成
  PBMediaAction pb = {};
  pb.mediaObject = MediaObject::decode;
  pb.trackType = trackType;
  pb.action = MediaAction::complete;
  pushPB<MPPBType::MediaAction>(mpPingback, pb);
}

// ffplay synchronize_audio
// 当与主时钟相差过大时，需要调整输出数据量
int64_t AudioTrack::syncAudio() {
  // 一个bufferMs的数据量
  int32_t wantSamples = frameSize;
  if (!mediaPlayer) {
    return wantSamples;
  }
  if (mediaPlayer->getSyncType() == SyncType::none) {
    return wantSamples;
  }
  Clock* mainClock = mediaPlayer->getMainClock();
  // 如果不是主时钟，需要与主时钟比较,确定输出数据量
  if (clock.get() != mainClock) {
    int64_t sClock = clock->clock();
    int64_t mClock = mainClock->clock();
    if (sClock == AVOX_NOVALID_PTS || mClock == AVOX_NOVALID_PTS) {
      diffAvgCount = 0;
      diffCum = 0;
      return wantSamples;
    }
    int64_t diff = clock->clock() - mainClock->clock();
    if (diff < AVOX_NOSYNC_THRESHOLD) {
      // 用于平滑时间差波动,累计误差
      diffCum = diff + diffAvgCoef * diffCum;
      // 收集足够的样本
      if (diffAvgCount < 20) {
        diffAvgCount++;
      } else {
        // 最终平均误差
        double avgDiff = diffCum * (1.0 - diffAvgCoef);
        // 假定超过50ms
        if (std::abs(avgDiff) > 50) {
          // 需要变动的值
          int64_t diffSize = diff * decodeDesc.sampleRate / 1000;
          // 允许采样变动范围，限定diffSize只有bufferSize浮动
          int64_t allowOffset = frameSize * 0.1;
          diffSize = std::min(allowOffset, std::max(-allowOffset, diffSize));
          wantSamples = frameSize + diffSize;
        }
      }
    }
  }
  return wantSamples;
}

void AudioTrack::pauseRender(bool pause) {
  if (renderTask) {
    renderTask->pause(pause);
  }
  clock->pause(pause);
}

void AudioTrack::pauseDecoder(bool pause) {
  if (decodeTask) {
    if (pause) {
      decodeTask->pauseTask();
    } else {
      decodeTask->resumeTask();
    }
  }
  // 记录解码暂停
  PBMediaAction pb = {};
  pb.mediaObject = MediaObject::decode;
  pb.trackType = trackType;
  pb.action = pause ? MediaAction::pause : MediaAction::resume;
  pushPB<MPPBType::MediaAction>(mpPingback, pb);
}

void AudioTrack::flush() {
  if (renderTask) {
    renderTask->flush();
  }
  if (decodeTask) {
    decodeTask->flush();
  }
  packetQueue.clear();
  frameQueue.clear();
  clock->reset();
  //
  curFrame.clear();
  curFrame.setPts(AVOX_NOVALID_PTS);
  startCheck = false;
  checkDataSize = 0;
  // 记录flush
  PBMediaAction pb = {};
  pb.mediaObject = MediaObject::track;
  pb.trackType = trackType;
  pb.action = MediaAction::flush;
  pushPB<MPPBType::MediaAction>(mpPingback, pb);
}

void AudioTrack::updateSeekTime(int64_t seekTime) {
  curFrame.setPts(seekTime);
  clock->update(seekTime);
}

void AudioTrack::close() {
  curFrame.setPts(AVOX_NOVALID_PTS);
  packetQueue.setClose(true);
  // 先通知frameQueue不可用，保证decodeTask顺利关闭
  frameQueue.setClose(true);
  if (renderTask) {
    renderTask->close();
    // 记录渲染关闭
    PBMediaAction pb = {};
    pb.mediaObject = MediaObject::render;
    pb.trackType = trackType;
    pb.action = MediaAction::close;
    pushPB<MPPBType::MediaAction>(mpPingback, pb);
  }
  if (decodeTask) {
    decodeTask->close();
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
