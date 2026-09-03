#include "BaseSource.hpp"

namespace avox {

BaseSource::BaseSource() { bOpen = false; }

void BaseSource::disableVideo(bool bDisable) {
  if (bOpen) {
    LOGFLF(LogLevel::warn, "already open, video cannot disable");
    return;
  }
  bDisableVideo = bDisable;
}

void BaseSource::disableAudio(bool bDisable) {
  if (bOpen) {
    LOGFLF(LogLevel::warn, "already open, audio cannot disable");
    return;
  }
  bDisableAudio = bDisable;
}

int32_t BaseSource::videoSize() { return videoTracks.size(); }

int32_t BaseSource::audioSize() { return audioTracks.size(); }

VTrackDesc BaseSource::getVideoDesc(int32_t index) {
  if (index < 0 || index >= videoTracks.size()) {
    return {};
  }
  return videoTracks[index];
}

ATrackDesc BaseSource::getAudioDesc(int32_t index) {
  if (index < 0 || index >= audioTracks.size()) {
    return {};
  }
  return audioTracks[index];
}

bool BaseSource::canSeek() { return bSeek; }

void BaseSource::addVideoDesc(const VTrackDesc& desc) {
  videoTracks.push_back(desc);
  LOGFLF(LogLevel::info, "add video track: ", desc);
}

void BaseSource::addAudioDesc(const ATrackDesc& desc) {
  audioTracks.push_back(desc);
  LOGFLF(LogLevel::info, "add audio track: ", desc);
}

void BaseSource::trackReady() {
  if (audioTracks.size() <= 0) {
    LOGFLF(LogLevel::info, "no audio track");
    bDisableAudio = true;
  }
  if (videoTracks.size() <= 0) {
    LOGFLF(LogLevel::info, "no video track");
    bDisableVideo = true;
  }
  onTrackOpen();
}

}
