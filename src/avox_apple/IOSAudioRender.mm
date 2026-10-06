#include "IOSAudioRender.hpp"

#include <TargetConditionals.h>
#include "avox/AvoxTime.h"
#include "avox/module/AvoxManager.hpp"
#include "avox/player/MediaPlayer.hpp"

namespace avox {

void regIOSAudioRender() {
  RegFunc regFunc = {
      "iOS AudioUnit render init", []() {
        ARenderDesc renderDesc = {};
        renderDesc.name = "iOS AudioUnit Render";
        AvoxManager::Get().aRender.regInitFunc(
            ARenderType::iosAU, renderDesc,
            []() -> AudioRender* { return new IOSAudioRender(); });
      }};
  AvoxManager::Get().initFuncs.push_back(regFunc);
}

IOSAudioRender::IOSAudioRender() {
#ifdef AVOX_ENABLE_FFMPEG
  resample = std::make_unique<FFResample>();
#endif
}

IOSAudioRender::~IOSAudioRender() { close(); }

AudioStreamBasicDescription audioFormatToASBD(const AudioDesc& desc, double sampleRate) {
  AudioStreamBasicDescription asbd = {};
  asbd.mSampleRate = sampleRate > 0 ? sampleRate : desc.sampleRate;
  asbd.mFormatID = kAudioFormatLinearPCM;
  asbd.mFormatFlags = kAudioFormatFlagIsPacked;
  asbd.mFramesPerPacket = 1;
  asbd.mChannelsPerFrame = desc.channels;

  switch (desc.format) {
    case AudioFormat::AVOX_AUDIO_U8:
      asbd.mBitsPerChannel = 8;
      asbd.mBytesPerFrame = desc.channels;
      asbd.mBytesPerPacket = desc.channels;
      break;
    case AudioFormat::AVOX_AUDIO_S16:
      asbd.mBitsPerChannel = 16;
      asbd.mFormatFlags |= kAudioFormatFlagIsSignedInteger;
      asbd.mBytesPerFrame = desc.channels * 2;
      asbd.mBytesPerPacket = desc.channels * 2;
      break;
    case AudioFormat::AVOX_AUDIO_S32:
      asbd.mBitsPerChannel = 32;
      asbd.mFormatFlags |= kAudioFormatFlagIsSignedInteger;
      asbd.mBytesPerFrame = desc.channels * 4;
      asbd.mBytesPerPacket = desc.channels * 4;
      break;
    case AudioFormat::AVOX_AUDIO_FLT:
      asbd.mBitsPerChannel = 32;
      asbd.mFormatFlags |= kAudioFormatFlagIsFloat;
      asbd.mBytesPerFrame = desc.channels * 4;
      asbd.mBytesPerPacket = desc.channels * 4;
      break;
    default:
      asbd.mBitsPerChannel = 16;
      asbd.mFormatFlags |= kAudioFormatFlagIsSignedInteger;
      asbd.mBytesPerFrame = desc.channels * 2;
      asbd.mBytesPerPacket = desc.channels * 2;
      break;
  }
  return asbd;
}

void IOSAudioRender::onInit() {
  std::unique_lock<std::mutex> lock(mtx);
  if (!desc.bValid()) {
    LOGFLF(LogLevel::warn, "desc is not valid");
    return;
  }

  // 设置音频会话(仅 iOS, macOS 无 AVAudioSession)
#if TARGET_OS_IPHONE
  NSError* error = nil;
  AVAudioSession* session = [AVAudioSession sharedInstance];
  [session setCategory:AVAudioSessionCategoryPlayback error:&error];
  if (error) {
    LOGFLF(LogLevel::warn, "Failed to set audio session category");
    return;
  }
  [session setActive:YES error:&error];
  if (error) {
    LOGFLF(LogLevel::warn, "Failed to activate audio session");
    return;
  }

  // 获取系统首选采样率
  renderDesc.sampleRate = session.sampleRate;
  renderDesc.channels = 2;  // iOS 立体声输出
  renderDesc.format = AudioFormat::AVOX_AUDIO_FLT;  // iOS 使用浮点格式
#else
  // macOS 无音频会话, 先按输入描述兜底, AU 创建后按设备真实格式覆盖
  renderDesc.sampleRate = desc.sampleRate;
  renderDesc.channels = desc.channels;
  renderDesc.format = desc.format;
#endif

  // 创建 Audio Unit(iOS 为 RemoteIO, macOS 为 DefaultOutput)
  AudioComponentDescription compDesc = {};
  compDesc.componentType = kAudioUnitType_Output;
#if TARGET_OS_IPHONE
  compDesc.componentSubType = kAudioUnitSubType_RemoteIO;
#else
  compDesc.componentSubType = kAudioUnitSubType_DefaultOutput;
#endif
  compDesc.componentManufacturer = kAudioUnitManufacturer_Apple;
  compDesc.componentFlags = 0;
  compDesc.componentFlagsMask = 0;

  AudioComponent component = AudioComponentFindNext(nullptr, &compDesc);
  if (!component) {
    LOGFLF(LogLevel::warn, "Failed to find audio component");
    return;
  }

  OSStatus status = AudioComponentInstanceNew(component, &audioUnit);
  if (status != noErr) {
    LOGFLF(LogLevel::warn, "Failed to create audio unit: %d", status);
    return;
  }

#if !TARGET_OS_IPHONE
  // 对齐 WASAPI GetMixFormat 语义: 以输出设备真实声道/采样率为下混目标。
  // 多声道直接设给 AU 时 AUHAL 按序直通, C(对白)/环绕声道被丢 —— FC 占
  // 96% 的素材只剩音乐环境声 (1002 实测); 查询失败按立体声兜底
  AudioStreamBasicDescription deviceFormat = {};
  UInt32 formatSize = sizeof(deviceFormat);
  if (AudioUnitGetProperty(audioUnit, kAudioUnitProperty_StreamFormat,
                           kAudioUnitScope_Output, 0, &deviceFormat,
                           &formatSize) == noErr &&
      deviceFormat.mChannelsPerFrame > 0) {
    renderDesc.channels = deviceFormat.mChannelsPerFrame;
    renderDesc.sampleRate = (int32_t)deviceFormat.mSampleRate;
    if (deviceFormat.mFormatFlags & kAudioFormatFlagIsFloat) {
      renderDesc.format = AudioFormat::AVOX_AUDIO_FLT;
    }
  } else {
    renderDesc.channels = 2;
  }
#endif

#ifdef AVOX_ENABLE_FFMPEG
  if (!resample->init(desc, renderDesc)) {
    LOGFLF(LogLevel::warn, "resample init failed");
  }
#endif

  // 设置输出格式
  AudioStreamBasicDescription outputFormat = audioFormatToASBD(renderDesc, renderDesc.sampleRate);
  status = AudioUnitSetProperty(audioUnit, kAudioUnitProperty_StreamFormat,
                                  kAudioUnitScope_Input, 0, &outputFormat, sizeof(outputFormat));
  if (status != noErr) {
    LOGFLF(LogLevel::warn, "Failed to set stream format: %d", status);
    AudioComponentInstanceDispose(audioUnit);
    audioUnit = nullptr;
    return;
  }

  // 设置回调
  AURenderCallbackStruct callbackStruct = {};
  callbackStruct.inputProc = renderCallback;
  callbackStruct.inputProcRefCon = this;
  status = AudioUnitSetProperty(audioUnit, kAudioUnitProperty_SetRenderCallback,
                                  kAudioUnitScope_Input, 0, &callbackStruct, sizeof(callbackStruct));
  if (status != noErr) {
    LOGFLF(LogLevel::warn, "Failed to set render callback: %d", status);
    AudioComponentInstanceDispose(audioUnit);
    audioUnit = nullptr;
    return;
  }

  // 初始化并启动
  status = AudioUnitInitialize(audioUnit);
  if (status != noErr) {
    LOGFLF(LogLevel::warn, "Failed to initialize audio unit: %d", status);
    AudioComponentInstanceDispose(audioUnit);
    audioUnit = nullptr;
    return;
  }

  // 分配环形缓冲区（约 200ms; 容量取2的幂便于掩码取模）
  uint32_t bufferMs = 200;
  uint32_t sampleSize = renderDesc.channels * audioFormatSize(renderDesc.format);
  if (sampleSize == 0) {
    LOGFLF(LogLevel::warn, "invalid render desc, no audio buffer");
    AudioUnitUninitialize(audioUnit);
    AudioComponentInstanceDispose(audioUnit);
    audioUnit = nullptr;
    return;
  }
  uint32_t want = renderDesc.sampleRate * bufferMs / 1000 * sampleSize;
  ringSize = 8;
  while (ringSize < want) {
    ringSize <<= 1;
  }
  ringMask = ringSize - 1;
  ringBuffer.assign(ringSize, 0);
  wPos.store(0, std::memory_order_relaxed);
  rPos.store(0, std::memory_order_relaxed);
  flushTarget.store(0, std::memory_order_relaxed);
  flushReq.store(false, std::memory_order_relaxed);
  totalFrames.store(0, std::memory_order_relaxed);
  playedFrames = 0;
  lastQueueMs = bufferMs / 2;
  // 诊断窗随重开归零(轨对象跨 open 复用, 残留计数会串片)
  silenceFrames = 0;
  dropFrames = 0;
  lateFeeds = 0;
  maxLateMs = 0;
  lastFeedMs = 0;
  lastDiagMs = 0;
  diagWindow = 0;

  status = AudioOutputUnitStart(audioUnit);
  if (status != noErr) {
    LOGFLF(LogLevel::warn, "Failed to start audio unit: %d", status);
    AudioUnitUninitialize(audioUnit);
    AudioComponentInstanceDispose(audioUnit);
    audioUnit = nullptr;
    return;
  }
  // 缓冲就绪且设备已起, 才置运行位(回调据此判断可读)
  running.store(true, std::memory_order_release);

  log(LogLevel::info, "iOS AudioUnit render init success, sampleRate:", renderDesc.sampleRate,
      " channels:", renderDesc.channels, " bufferMs:", bufferMs);
}

OSStatus IOSAudioRender::renderCallback(void* inRefCon,
                                         AudioUnitRenderActionFlags* ioActionFlags,
                                         const AudioTimeStamp* inTimeStamp,
                                         UInt32 inBusNumber,
                                         UInt32 inNumberFrames,
                                         AudioBufferList* ioData) {
  IOSAudioRender* self = static_cast<IOSAudioRender*>(inRefCon);
  // 实时线程全程无锁: 只读写原子索引。历史上此处用互斥量+try_lock, 抢不到锁即
  // 整块(21ms)静音, 实测占播放时长 2.35% —— 是 iOS/mac 声音不连续的直接原因
  if (!ioData || !self->running.load(std::memory_order_acquire)) {
    return noErr;
  }
  // 冲刷请求(seek 重定位): 由本线程兑现 —— 它才是 rPos 的唯一写者
  if (self->flushReq.exchange(false, std::memory_order_acq_rel)) {
    self->rPos.store(self->flushTarget.load(std::memory_order_acquire),
                     std::memory_order_release);
  }

  uint32_t sampleSize = self->renderDesc.channels * audioFormatSize(self->renderDesc.format);
  uint32_t bytesNeeded = inNumberFrames * sampleSize;
  uint32_t avail = self->wPos.load(std::memory_order_acquire) -
                   self->rPos.load(std::memory_order_relaxed);  // 无符号环绕安全
  uint32_t pos = self->rPos.load(std::memory_order_relaxed);
  uint32_t silent = 0;

  for (UInt32 i = 0; i < ioData->mNumberBuffers; i++) {
    AudioBuffer* buffer = &ioData->mBuffers[i];
    uint8_t* outData = (uint8_t*)buffer->mData;
    uint32_t bytesToCopy = std::min(bytesNeeded, buffer->mDataByteSize);
    uint32_t n = std::min(avail, bytesToCopy);

    if (n) {
      uint32_t idx = pos & self->ringMask;
      uint32_t firstPart = std::min(n, self->ringSize - idx);
      memcpy(outData, self->ringBuffer.data() + idx, firstPart);
      if (firstPart < n) {
        memcpy(outData + firstPart, self->ringBuffer.data(), n - firstPart);
      }
      pos += n;
      avail -= n;
    }
    if (n < bytesToCopy) {
      // 只补缺的那一段(旧实现整块静音, 还把已到的数据推迟一整块)
      memset(outData + n, 0, bytesToCopy - n);
      silent += (bytesToCopy - n) / sampleSize;
    }
  }
  self->rPos.store(pos, std::memory_order_release);
  self->playedFrames += inNumberFrames;
  // 起播前导(尚未喂过数据)不算丢音
  if (silent && self->totalFrames.load(std::memory_order_relaxed) > 0) {
    self->silenceFrames.fetch_add(silent, std::memory_order_relaxed);
  }

  return noErr;
}

void IOSAudioRender::onRender(const AvoxData& frame) {
  // 供给节拍: 生产线程按帧长节拍喂, 明显超时=被抢占或IO卡, 先于回调欠载暴露
  int64_t nowMs = timeStampMS();
  if (lastFeedMs > 0) {
    int64_t gap = nowMs - lastFeedMs;
    if (gap > (int64_t)frameMs * 3 / 2) {
      lateFeeds.fetch_add(1, std::memory_order_relaxed);
      uint32_t prev = maxLateMs.load(std::memory_order_relaxed);
      while ((int64_t)gap > (int64_t)prev &&
             !maxLateMs.compare_exchange_weak(prev, (uint32_t)gap,
                                              std::memory_order_relaxed)) {
      }
    }
  }
  lastFeedMs = nowMs;

  if (!running.load(std::memory_order_acquire) || frame.size == 0) {
    return;
  }

  AvoxData inData = frame;
#ifdef AVOX_ENABLE_FFMPEG
  if (!resample->resample(inData)) {
    LOGFLF(LogLevel::warn, "resample failed");
    return;
  }
#endif

  // 生产者独占写 wPos, 消费者独占写 rPos(SPSC 无锁契约, 不取锁)
  uint32_t bytes = (uint32_t)inData.size;
  uint32_t sampleSize = renderDesc.channels * audioFormatSize(renderDesc.format);
  uint32_t w = wPos.load(std::memory_order_relaxed);
  uint32_t used = w - rPos.load(std::memory_order_acquire);
  if (bytes > 0 && used <= ringSize && bytes <= ringSize - used) {
    uint32_t idx = w & ringMask;
    uint32_t firstPart = std::min(bytes, ringSize - idx);
    memcpy(ringBuffer.data() + idx, inData.data, firstPart);
    if (firstPart < bytes) {
      memcpy(ringBuffer.data(), inData.data + firstPart, bytes - firstPart);
    }
    wPos.store(w + bytes, std::memory_order_release);
    totalFrames.fetch_add(bytes / sampleSize, std::memory_order_relaxed);
  } else if (bytes > 0) {
    // 塞不下则整帧丢弃(不做部分写), 是一条静默的数据丢失路径
    dropFrames.fetch_add(1, std::memory_order_relaxed);
  }
  reportDiag(nowMs);
}

void IOSAudioRender::reportDiag(int64_t nowMs) {
  if (lastDiagMs == 0) {
    lastDiagMs = nowMs;
    return;
  }
  if (nowMs - lastDiagMs < 2000) {
    return;
  }
  int32_t sr = renderDesc.sampleRate > 0 ? renderDesc.sampleRate : 48000;
  uint64_t sil = silenceFrames.exchange(0, std::memory_order_relaxed);
  uint64_t df = dropFrames.exchange(0, std::memory_order_relaxed);
  uint64_t late = lateFeeds.exchange(0, std::memory_order_relaxed);
  uint32_t ml = maxLateMs.exchange(0, std::memory_order_relaxed);
  lastDiagMs = nowMs;
  // 静默期不留痕, 每10窗留一条心跳证链路在测
  ++diagWindow;
  if (!sil && !df && !late && (diagWindow % 10) != 0) {
    return;
  }
  LOGFLF(LogLevel::info, "[audio-diag] silence:", (int64_t)(sil * 1000 / sr),
         "ms dropFrames:", (int64_t)df, " lateFeeds:", (int64_t)late,
         " maxLate:", (int64_t)ml, "ms queue:", getQueueMS(), "ms sr:", sr);
}

bool IOSAudioRender::empty() {
  if (!running.load(std::memory_order_acquire) || ringSize == 0) {
    return true;
  }
  return getQueueMS() < frameMs;
}

int32_t IOSAudioRender::getQueueMS() {
  if (!running.load(std::memory_order_acquire) || ringSize == 0 ||
      renderDesc.sampleRate <= 0) {
    return lastQueueMs;
  }
  // 查询路径不取锁: 只读两个原子索引(SPSC)
  uint32_t used = wPos.load(std::memory_order_acquire) -
                  rPos.load(std::memory_order_relaxed);
  uint32_t sampleSize = renderDesc.channels * audioFormatSize(renderDesc.format);
  return (int32_t)((used / sampleSize) * 1000 / renderDesc.sampleRate);
}

bool IOSAudioRender::full() {
  if (!running.load(std::memory_order_acquire) || ringSize == 0) {
    return false;
  }
  return getQueueMS() >= frameMs * 2;
}

void IOSAudioRender::pause(bool pause) {
  AudioComponentInstance unit = nullptr;
  {
    std::unique_lock<std::mutex> lock(mtx);
    unit = audioUnit;
    lastFeedMs = 0;  // 暂停跨度不算供给卡顿
  }
  if (!unit) {
    return;
  }

  if (pause) {
    AudioOutputUnitStop(unit);
  } else {
    AudioOutputUnitStart(unit);
  }
}

void IOSAudioRender::flush() {
  // 丢弃冲刷点之前的已写数据; 由回调兑现(它才是 rPos 的唯一写者), 之后写入的新帧保留
  flushTarget.store(wPos.load(std::memory_order_acquire), std::memory_order_release);
  flushReq.store(true, std::memory_order_release);
  lastFeedMs = 0;  // seek 冲刷跨度不算供给卡顿
}

void IOSAudioRender::onClose() {
  // 先落运行位(回调据此提前退出), 再停/销毁设备(stop 返回即无在飞回调),
  // 最后才动缓冲 —— 全程不需要实时回调取锁
  running.store(false, std::memory_order_release);
  AudioComponentInstance unit = nullptr;
  {
    // 锁内只摘句柄: 持锁调 AudioOutputUnitStop 会等实时回调让路
    std::unique_lock<std::mutex> lock(mtx);
    unit = audioUnit;
    audioUnit = nullptr;
  }
  if (unit) {
    AudioOutputUnitStop(unit);
    AudioUnitUninitialize(unit);
    AudioComponentInstanceDispose(unit);
  }
  {
    std::unique_lock<std::mutex> lock(mtx);
    ringBuffer.clear();
  }

#if TARGET_OS_IPHONE
  AVAudioSession* session = [AVAudioSession sharedInstance];
  [session setActive:NO error:nil];
#endif

  log(LogLevel::info, "iOS AudioUnit render close");
}

void IOSAudioRender::speed(double speed) {
  // iOS AudioUnit 不直接支持变速，需要在应用层处理
}

void IOSAudioRender::setVolume(float cvolume) {
  AudioComponentInstance unit = nullptr;
  float v = 0.0f;
  {
    std::unique_lock<std::mutex> lock(mtx);
    volume = std::max(0.0f, std::min(1.0f, cvolume));
    v = volume;
    unit = audioUnit;
  }
  // AudioUnitSetParameter 同样进 HAL: 持锁调它会与实时回调互等
  if (unit) {
    AudioUnitSetParameter(unit, kHALOutputParam_Volume,
                          kAudioUnitScope_Global, 0, v, 0);
  }
}

float IOSAudioRender::getVolume() {
  return volume;
}

}
