#include "DeviceSource.hpp"

#include "../module/AvoxManager.hpp"

namespace avox {

void regDeviceRawSource() {
  RegFunc regFunc = {"device raw source init", []() {
                       RawSourceDesc rawSource = {};
                       rawSource.name = "device source";
                       AvoxManager::Get().rawSources.regInitFunc(
                           RawSourceType::device, rawSource,
                           []() -> RawSource* { return new DeviceSource(); });
                     }};
  AvoxManager::Get().initFuncs.push_back(regFunc);
}

DeviceSource::DeviceSource() {
  // 默认什么设备都没,关闭音频与视频
  bDisableAudio = true;
  bDisableVideo = true;
}

void DeviceSource::enableDefaultAudioSource() {
  if (audioSource) {
    return;
  }
  ADeviceSdk sdk = getDefaltAudioSdk();
  if (sdk == ADeviceSdk::none) {
    return;
  }
  IAudioManager* mgr = AvoxManager::Get().aDeviceMgr.getMgr(sdk);
  if (!mgr) {
    return;
  }
  IAudioSource* source = mgr->getDevice(0);
  setAudioSource(source);
}

void DeviceSource::enableDefaultVideoSource() {
  if (videoSource) {
    return;
  }
  IVideoManager* mgr =
      AvoxManager::Get().vDeviceMgr.getMgr(VDeviceSdk::win_capture);
  if (!mgr) {
    return;
  }
  IVideoSource* source = mgr->getDevice(0);
  setVideoSource(source);
}

void DeviceSource::openAudioSource() {
  if (bDisableAudio) {
    LOGFLF(LogLevel::info, "user disable audio source");
    return;
  }
  if (audioSource) {
    audioSource->addObserver(this);
    bool result = audioSource->open();
    LOGFLF(LogLevel::info, "audio source open:", result);
  }
}

void DeviceSource::openVideoSource() {
  if (bDisableVideo) {
    LOGFLF(LogLevel::info, "user disable video source");
    return;
  }
  if (videoSource) {
    videoSource->addObserver(this);
    bool result = videoSource->open();
    LOGFLF(LogLevel::info, "video source open:", result);
  }
}

void DeviceSource::closeAudioSource() {
  if (audioSource) {
    audioSource->close();
    audioSource->removeObserver(this);
  }
}

void DeviceSource::closeVideoSource() {
  if (videoSource) {
    videoSource->close();
    videoSource->removeObserver(this);
  }
}

void DeviceSource::setAudioSource(IAudioSource* asource) {
  AudioSource* source = dynamic_cast<AudioSource*>(asource);
  // AVOX_REMOVE_OBSERVER(audioSource, source)
  if (audioSource == source) {
    return;
  }
  // 关闭老的
  closeAudioSource();
  audioSource = source;
  // 设置了音频设备,表示用户希望有音频
  bDisableAudio = audioSource == nullptr;
  // 如果是打开状态，打开新的
  openAudioSource();
}

void DeviceSource::setVideoSource(IVideoSource* vsource) {
  VideoSource* source = dynamic_cast<VideoSource*>(vsource);
  // AVOX_REMOVE_OBSERVER(videoSource, source)
  if (videoSource == source) {
    return;
  }
  // 关闭老的
  closeVideoSource();
  videoSource = source;
  // 设置了视频设备
  bDisableVideo = videoSource == nullptr;
  // 如果是打开状态，打开新的
  openVideoSource();
}

bool DeviceSource::open() {
  RawSource::open();
  openAudioSource();
  openVideoSource();
  return true;
}

void DeviceSource::close() {
  closeAudioSource();
  closeVideoSource();
  RawSource::close();
}

bool DeviceSource::bOpening() {
  // 如果enableAudio，则需要audioSource打开
  // 如果enableVideo，则需要videoSource打开
  if (!bDisableAudio) {
    if (!audioSource || !audioSource->bOpening()) {
      return false;
    }
  }
  if (!bDisableVideo) {
    if (!videoSource || !videoSource->bOpening()) {
      return false;
    }
  }
  return true;
}

void DeviceSource::onAudioDesc(const AudioDesc& desc) { setAudioDesc(desc); }

void DeviceSource::onVideoDesc(const VideoDesc& desc) { setVideoDesc(desc); }

void DeviceSource::onAudioFrame(const AvoxAFrame& frame) { pushFrame(frame); }
void DeviceSource::onVideoFrame(const YUVFrame& frame) { pushFrame(frame); }
void DeviceSource::onGpuFrame(const GpuFrame& frame) { pushFrame(frame); }

void DeviceSource::onAudioError(AVError error, const char* msg) {
  dispatch(&IRawSourceOb::onError, error, msg);
}
void DeviceSource::onVideoError(AVError error, const char* msg) {
  dispatch(&IRawSourceOb::onError, error, msg);
}

void DeviceSource::onAudioClose() {}

void DeviceSource::onVideoClose() {}

}