#include "RawSource.hpp"

namespace avox {

RawSource::RawSource() {}

RawSource::~RawSource() {}

bool RawSource::open() {
  videoTracks.clear();
  audioTracks.clear();
  bOpen = true;
  bReady = false;
  // 等待设置音频与视频描述
  return true;
}

void RawSource::setVideoDesc(const VideoDesc& desc) {
  VTrackDesc vdesc = {};
  vdesc.desc = desc;
  if (videoTracks.size() == 0) {
    addVideoDesc(vdesc);
  } else {
    videoTracks[0] = vdesc;
  }
  checkTrackReady();
}

void RawSource::setAudioDesc(const AudioDesc& desc) {
  ATrackDesc adesc = {};
  adesc.desc = desc;
  if (audioTracks.size() == 0) {
    addAudioDesc(adesc);
  } else {
    audioTracks[0] = adesc;
  }
  checkTrackReady();
}

void RawSource::checkTrackReady() {
  std::lock_guard<std::mutex> lock(avMtx);
  // 音频启用，则需要音频描述
  if (!bDisableAudio && audioTracks.empty()) {
    return;
  }
  // 视频启用，则需要视频描述
  if (!bDisableVideo && videoTracks.empty()) {
    return;
  }
  trackReady();
}

void RawSource::updateExpectVideo(bool bExpect) {
  {
    std::lock_guard<std::mutex> lock(avMtx);
    // 幂等: 期望未变化不重触发trackReady
    if (bDisableVideo == !bExpect) {
      return;
    }
    bDisableVideo = !bExpect;
  }
  checkTrackReady();
}

void RawSource::updateExpectAudio(bool bExpect) {
  {
    std::lock_guard<std::mutex> lock(avMtx);
    if (bDisableAudio == !bExpect) {
      return;
    }
    bDisableAudio = !bExpect;
  }
  checkTrackReady();
}

void RawSource::onTrackOpen() {
  // 幂等: 已ready后重协商再触发trackReady不重复派发onReady(观察者重入会重开muxer)
  if (bReady) {
    return;
  }
  bReady = true;
  dispatch(&IRawSourceOb::onReady);
}

void RawSource::close() {
  bReady = false;
  bOpen = false;
  dispatch(&IRawSourceOb::onClose);
}

bool RawSource::bOpening() { return bReady; }

ISourceInfo* RawSource::getSourceInfo() { return this; }

void RawSource::pushFrame(const YUVFrame& frame) {
  if (!bReady) {
    return;
  }
  dispatch(&IRawSourceOb::onVideoFrame, frame, 0);
}

void RawSource::pushFrame(const GpuFrame& frame) {
  if (!bReady) {
    return;
  }
  // HighClock clock = {};
  dispatch(&IRawSourceOb::onGpuFrame, frame, 0);
  // log(LogLevel::debug, "forward source cost1:", clock.recordDelta());
}

void RawSource::pushFrame(const AvoxAFrame& frame) {
  if (!bReady) {
    return;
  }
  dispatch(&IRawSourceOb::onAudioFrame, frame, 0);
}

}
