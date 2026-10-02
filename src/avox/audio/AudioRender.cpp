#include "AudioRender.hpp"

#include "../module/AvoxManager.hpp"
#include "../player/AudioTrack.hpp"
#include "AudioLeveler.hpp"

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
  // 响度均衡按新 desc 重建(新流=重新收敛)
  {
    std::lock_guard<std::mutex> lk(levelerMtx);
    createAudioLevelerLocked();
  }
  onInit();
  // 延迟 tap:desc 就绪后自动 open
  if (bTapPending) {
    openTap(pendingTapOutDesc, pendingTapFrameMs);
  }
}

void AudioRender::close() {
  onClose();
  frameSize = 0;
  // 锁放在设备关闭之后: 渲染线程可能正持锁处理, 不跨 onClose 持锁防互等
  std::lock_guard<std::mutex> lk(levelerMtx);
  if (audioLeveler) {
    audioLeveler->reset();
  }
}

void AudioRender::setTapBlock(bool b) {
  bTapBlock = b;
  // tap 在:快照后同步(锁外);不在:只存成员,创建时取 —— 与 set/openTap 顺序无关
  std::shared_ptr<AudioTap> tap;
  {
    std::lock_guard<std::mutex> lk(tapMtx);
    tap = audioTap;
  }
  if (tap) {
    tap->setBlock(b);
  }
}

void AudioRender::onAudioProcess(const AvoxAFrame& frame) {
  if (!closeOutput) {
    onRender(frame.buffer);
  }
  // tap:深拷贝入队(在处理/设备之后,取 post-3A 帧);快照锁外 push(T18)
  std::shared_ptr<AudioTap> tap;
  {
    std::lock_guard<std::mutex> lk(tapMtx);
    tap = audioTap;
  }
  if (tap && tap->bOpen()) {
    tap->push(frame.buffer, frame.pts);
  }
}

void AudioRender::enableAec(const AudioAec& aec) { enableProcess = true; }

void AudioRender::disableAec() { enableProcess = false; }

void AudioRender::setAudioNormalize(bool bEnable, double targetLufs) {
  std::lock_guard<std::mutex> lk(levelerMtx);
  bAudioNormalize = bEnable;
  audioNormalizeTarget = targetLufs;
  if (!bEnable) {
    audioLeveler.reset();
    return;
  }
  createAudioLevelerLocked();
}

void AudioRender::createAudioLevelerLocked() {
  if (!bAudioNormalize) {
    audioLeveler.reset();
    return;
  }
  if (!desc.bValid()) {
    // desc 未就绪: setDesc 时会再建
    audioLeveler.reset();
    return;
  }
  if (!audioLeveler) {
    audioLeveler = std::make_unique<AudioLeveler>();
  }
  if (!audioLeveler->init(desc, audioNormalizeTarget)) {
    audioLeveler.reset();
    log(LogLevel::warn, "audio normalize bypass, unsupported desc: ", desc);
    return;
  }
  LOGFLF(LogLevel::info, "audio normalize on, target:", audioNormalizeTarget,
         " LUFS, desc: ", desc);
}

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
  // 创建/取快照持 tapMtx, open 在锁外(open 内部 close 会 join 旧线程)
  std::shared_ptr<AudioTap> tap;
  {
    std::lock_guard<std::mutex> lk(tapMtx);
    if (!audioTap) {
      audioTap = std::make_shared<AudioTap>();
      // 新建 tap 时从成员取阻塞策略
      audioTap->setBlock(bTapBlock);
    }
    tap = audioTap;
  }
  tap->open(desc, outDesc, frameMs);
  // 与 setTapBlock 调用顺序无关(open 后重申)
  tap->setBlock(bTapBlock);
}

void AudioRender::closeTap() {
  bTapPending = false;
  // 锁内 move-out(T18): 渲染线程此后快照为空不再 push, 已持有快照的 push
  // 由 AudioTap bRunning/setClose 兜底; close(join)在锁外, 防反压互相等
  std::shared_ptr<AudioTap> tap;
  {
    std::lock_guard<std::mutex> lk(tapMtx);
    tap = std::move(audioTap);
  }
  if (tap) {
    tap->close();
  }
}

void AudioRender::addTapOb(IAudioTapOb* ob) {
  if (!ob) {
    return;
  }
  std::shared_ptr<AudioTap> tap;
  {
    std::lock_guard<std::mutex> lk(tapMtx);
    if (!audioTap) {
      audioTap = std::make_shared<AudioTap>();
      // 新建 tap 时从成员取阻塞策略
      audioTap->setBlock(bTapBlock);
    }
    tap = audioTap;
  }
  tap->addObserver(ob);
}

void AudioRender::removeTapOb(IAudioTapOb* ob) {
  if (!ob) {
    return;
  }
  std::shared_ptr<AudioTap> tap;
  {
    std::lock_guard<std::mutex> lk(tapMtx);
    tap = audioTap;
  }
  if (!tap) {
    return;
  }
  tap->removeObserver(ob);
}

void AudioRender::render(const AvoxData& frame, int64_t pts) {
  // 播放响度均衡: 输出走归一化缓冲, 不动原帧(相机录制/复用同一原始帧)
  AvoxData normalized = {};
  const AvoxData* out = &frame;
  {
    std::lock_guard<std::mutex> lk(levelerMtx);
    if (audioLeveler && audioLeveler->process(frame.data, frame.size, normalized)) {
      out = &normalized;
    }
  }
  if (initProcess) {
    AvoxAFrame inData = {};
    inData.buffer = *out;
    inData.pts = pts;
    audioProcess->process(inData);
    return;
  }
  if (frameSize == 0) {
    log(LogLevel::warn, "renderDesc is not valid");
    return;
  }
  if (!closeOutput) {
    onRender(*out);
  }
  // tap:深拷贝入队(在处理/设备之后,取 post-3A 帧);快照锁外 push(T18)
  std::shared_ptr<AudioTap> tap;
  {
    std::lock_guard<std::mutex> lk(tapMtx);
    tap = audioTap;
  }
  if (tap && tap->bOpen()) {
    tap->push(*out, pts);
  }
}

ARenderType getDefaultAudioType() {
#ifdef _WIN32
  return ARenderType::wasapi;
#elif __ANDROID__
  return ARenderType::androidAT;
#elif __APPLE__
  return ARenderType::iosAU;
#elif defined(__ONLY_LINUX__)
  // libpulse 缺失时未注册, getDefaultAudioOutput 会降级并告警
  return ARenderType::pulse;
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
