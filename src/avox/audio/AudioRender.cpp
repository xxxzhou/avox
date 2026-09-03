#include "AudioRender.hpp"

#include "../module/AvoxManager.hpp"
#include "../player/AudioTrack.hpp"

namespace avox {

AudioRender::AudioRender() { enableProcess = false; }

AudioRender::~AudioRender() { closeTap(); }

void AudioRender::setDesc(AudioDesc desc_, int32_t frameMs_) {
  desc = desc_;
  initProcess = false;
  if (enableProcess) {
    audioProcess = std::unique_ptr<AudioProcess>(createWebRtcAudioProcess());
    if (audioProcess) {
      initProcess = audioProcess->init(desc);
      if (initProcess) {
        audioProcess->setObserver(this);
      }
    }
  }
  frameMs = frameMs_;
  if (frameMs <= 0) {
    frameMs = 40;
  }
  frameSize = getAudioFrameSize(desc, frameMs);
  LOGFLF(LogLevel::info, "initProcess:", initProcess, " src desc: ", desc,
         " frameMs:", frameMs, " framesize:", frameSize);
  onInit();
  // 延迟 tap:desc 就绪后自动 open
  if (bTapPending) {
    openTap(pendingTapOutDesc, pendingTapFrameMs);
  }
}

void AudioRender::close() {
  onClose();
  frameSize = 0;
}

void AudioRender::setTapBlock(bool b) {
  bTapBlock = b;
  // tap 在:直接同步;不在:只存成员,创建时取 —— 与 set/openTap 顺序无关
  if (audioTap) {
    audioTap->setBlock(b);
  }
}

void AudioRender::onAudioProcess(const AvoxAFrame& frame) {
  if (!closeOutput) {
    onRender(frame.buffer);
  }
  // tap:深拷贝入队(在处理/设备之后,取 post-3A 帧)
  if (audioTap && audioTap->bOpen()) {
    audioTap->push(frame.buffer, frame.pts);
  }
}

void AudioRender::enableAec(const AudioAec& aec) { enableProcess = true; }

void AudioRender::disableAec() { enableProcess = false; }

void AudioRender::openTap(const AudioDesc& outDesc, int32_t frameMs) {
  if (frameSize <= 0) {
    // desc 未就绪,缓存参数,setDesc 后自动 open
    bTapPending = true;
    pendingTapOutDesc = outDesc;
    pendingTapFrameMs = frameMs;
    LOGFLF(LogLevel::info,
           "openTap pending: src desc not ready, will open after setDesc");
    return;
  }
  bTapPending = false;
  if (!audioTap) {
    audioTap = std::make_unique<AudioTap>();
  }
  audioTap->open(desc, outDesc, frameMs);
  // 从成员取,与 setTapBlock/openTap 调用顺序无关
  audioTap->setBlock(bTapBlock);
}

void AudioRender::closeTap() {
  bTapPending = false;
  if (audioTap) {
    audioTap->close();
    audioTap.reset();
  }
}

void AudioRender::addTapOb(IAudioTapOb* ob) {
  if (!ob) {
    return;
  }
  if (!audioTap) {
    audioTap = std::make_unique<AudioTap>();
    // 新建 tap 时从成员取阻塞策略
    audioTap->setBlock(bTapBlock);
  }
  audioTap->addObserver(ob);
}

void AudioRender::removeTapOb(IAudioTapOb* ob) {
  if (!audioTap || !ob) {
    return;
  }
  audioTap->removeObserver(ob);
}

void AudioRender::render(const AvoxData& frame, int64_t pts) {
  if (initProcess) {
    AvoxAFrame inData = {};
    inData.buffer = frame;
    inData.pts = pts;
    audioProcess->process(inData);
    return;
  }
  if (frameSize == 0) {
    log(LogLevel::warn, "renderDesc is not valid");
    return;
  }
  if (!closeOutput) {
    onRender(frame);
  }
  // tap:深拷贝入队(在处理/设备之后,取 post-3A 帧)
  if (audioTap && audioTap->bOpen()) {
    audioTap->push(frame, pts);
  }
}

ARenderType getDefaultAudioType() {
#ifdef _WIN32
  return ARenderType::wasapi;
#elif __ANDROID__
  return ARenderType::androidAT;
#elif __APPLE__
  return ARenderType::iosAU;
#endif
  return ARenderType::none;
}

ADeviceSdk getDefaltAudioSdk() {
#ifdef _WIN32
  return ADeviceSdk::wasapi;
#elif __ANDROID__
  return ADeviceSdk::android;
#elif __APPLE__
  return ADeviceSdk::ios;
#endif
  return ADeviceSdk::none;
}

// getDefaultAudioOutput 已移到 AudioOutput.cpp

}
