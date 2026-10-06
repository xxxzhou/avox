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

  // 分配环形缓冲区（约 200ms）
  uint32_t bufferMs = 200;
  uint32_t sampleSize = renderDesc.channels * audioFormatSize(renderDesc.format);
  bufferCapacity = renderDesc.sampleRate * bufferMs / 1000 * sampleSize;
  ringBuffer.resize(bufferCapacity);
  writePos = 0;
  readPos = 0;
  totalFrames = 0;
  playedFrames = 0;
  lastQueueMs = bufferMs / 2;
  // 诊断窗随重开归零(轨对象跨 open 复用, 残留计数会串片)
  underrunFrames = 0;
  lockBusyFrames = 0;
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
  // 实时线程绝不阻塞: 拿不到锁就出静音, 持锁等待会与 onClose/pause 的停流互等死锁
  std::unique_lock<std::mutex> lock(self->mtx, std::try_to_lock);
  if (!lock.owns_lock()) {
    if (ioData) {
      for (UInt32 i = 0; i < ioData->mNumberBuffers; i++) {
        memset(ioData->mBuffers[i].mData, 0, ioData->mBuffers[i].mDataByteSize);
      }
    }
    // 抢锁失败=整块静音, 可闻丢音; 只累加不写日志
    self->lockBusyFrames.fetch_add(inNumberFrames, std::memory_order_relaxed);
    return noErr;
  }

  if (!self->audioUnit || !ioData) {
    return noErr;
  }

  uint32_t sampleSize = self->renderDesc.channels * audioFormatSize(self->renderDesc.format);
  uint32_t bytesNeeded = inNumberFrames * sampleSize;
  uint32_t undFrames = 0;

  for (UInt32 i = 0; i < ioData->mNumberBuffers; i++) {
    AudioBuffer* buffer = &ioData->mBuffers[i];
    uint8_t* outData = (uint8_t*)buffer->mData;
    uint32_t bytesToCopy = std::min(bytesNeeded, buffer->mDataByteSize);

    // 从环形缓冲区读取数据
    uint32_t available = (self->writePos >= self->readPos)
                             ? (self->writePos - self->readPos)
                             : (self->bufferCapacity - self->readPos + self->writePos);

    if (available >= bytesToCopy) {
      // 读取数据
      uint32_t firstPart = std::min(bytesToCopy, self->bufferCapacity - self->readPos);
      memcpy(outData, self->ringBuffer.data() + self->readPos, firstPart);
      self->readPos = (self->readPos + firstPart) % self->bufferCapacity;

      if (firstPart < bytesToCopy) {
        memcpy(outData + firstPart, self->ringBuffer.data(), bytesToCopy - firstPart);
        self->readPos = bytesToCopy - firstPart;
      }

      self->playedFrames += inNumberFrames;
    } else {
      // 缓冲区数据不足，填充静音; 首次喂数据前的静音是起播前导, 不计丢音
      memset(outData, 0, bytesToCopy);
      if (self->totalFrames > 0) {
        undFrames += inNumberFrames;
      }
    }
  }
  if (undFrames) {
    self->underrunFrames.fetch_add(undFrames, std::memory_order_relaxed);
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

  std::unique_lock<std::mutex> lock(mtx);
  if (!audioUnit || frame.size == 0) {
    return;
  }

  AvoxData inData = frame;
#ifdef AVOX_ENABLE_FFMPEG
  if (!resample->resample(inData)) {
    LOGFLF(LogLevel::warn, "resample failed");
    return;
  }
#endif

  // 写入环形缓冲区
  uint32_t available = (writePos >= readPos)
                           ? (bufferCapacity - writePos + readPos)
                           : (readPos - writePos);

  if (inData.size <= available) {
    uint32_t firstPart = std::min((uint32_t)inData.size, bufferCapacity - writePos);
    memcpy(ringBuffer.data() + writePos, inData.data, firstPart);
    writePos = (writePos + firstPart) % bufferCapacity;

    if (firstPart < inData.size) {
      memcpy(ringBuffer.data(), inData.data + firstPart, inData.size - firstPart);
      writePos = inData.size - firstPart;
    }

    totalFrames += inData.size / (renderDesc.channels * audioFormatSize(renderDesc.format));
  } else {
    // 塞不下则整帧丢弃(不做部分写), 是一条静默的数据丢失路径
    dropFrames.fetch_add(1, std::memory_order_relaxed);
  }
  lock.unlock();
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
  uint64_t uf = underrunFrames.exchange(0, std::memory_order_relaxed);
  uint64_t lf = lockBusyFrames.exchange(0, std::memory_order_relaxed);
  uint64_t df = dropFrames.exchange(0, std::memory_order_relaxed);
  uint64_t late = lateFeeds.exchange(0, std::memory_order_relaxed);
  uint32_t ml = maxLateMs.exchange(0, std::memory_order_relaxed);
  lastDiagMs = nowMs;
  // 静默期不留痕, 每10窗留一条心跳证链路在测
  ++diagWindow;
  if (!uf && !lf && !df && !late && (diagWindow % 10) != 0) {
    return;
  }
  LOGFLF(LogLevel::info, "[audio-diag] underrun:", (int64_t)(uf * 1000 / sr),
         "ms lockBusy:", (int64_t)(lf * 1000 / sr), "ms dropFrames:", (int64_t)df,
         " lateFeeds:", (int64_t)late, " maxLate:", (int64_t)ml,
         "ms queue:", getQueueMS(), "ms sr:", sr);
}

bool IOSAudioRender::empty() {
  std::unique_lock<std::mutex> lock(mtx);
  if (!audioUnit) {
    return true;
  }

  uint32_t available = (writePos >= readPos)
                           ? (writePos - readPos)
                           : (bufferCapacity - readPos + writePos);
  uint32_t sampleSize = renderDesc.channels * audioFormatSize(renderDesc.format);
  int32_t availableMs = (available / sampleSize) * 1000 / renderDesc.sampleRate;
  return availableMs < frameMs;
}

int32_t IOSAudioRender::getQueueMS() {
  std::unique_lock<std::mutex> lock(mtx);
  if (!audioUnit) {
    return lastQueueMs;
  }

  uint32_t available = (writePos >= readPos)
                           ? (writePos - readPos)
                           : (bufferCapacity - readPos + writePos);
  uint32_t sampleSize = renderDesc.channels * audioFormatSize(renderDesc.format);
  int32_t queueMs = (available / sampleSize) * 1000 / renderDesc.sampleRate;
  return queueMs;
}

bool IOSAudioRender::full() {
  std::unique_lock<std::mutex> lock(mtx);
  if (!audioUnit) {
    return false;
  }

  uint32_t available = (writePos >= readPos)
                           ? (writePos - readPos)
                           : (bufferCapacity - readPos + writePos);
  uint32_t sampleSize = renderDesc.channels * audioFormatSize(renderDesc.format);
  int32_t queueMs = (available / sampleSize) * 1000 / renderDesc.sampleRate;
  return queueMs >= frameMs * 2;
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
  std::unique_lock<std::mutex> lock(mtx);
  writePos = 0;
  readPos = 0;
  totalFrames = 0;
  playedFrames = 0;
  lastQueueMs = 0;
  lastFeedMs = 0;  // seek 冲刷跨度不算供给卡顿
}

void IOSAudioRender::onClose() {
  AudioComponentInstance unit = nullptr;
  {
    // 锁内只摘句柄: 持锁调 AudioOutputUnitStop 会等实时回调让路, 而回调在等这把锁
    std::unique_lock<std::mutex> lock(mtx);
    unit = audioUnit;
    audioUnit = nullptr;
    ringBuffer.clear();
  }
  if (unit) {
    AudioOutputUnitStop(unit);
    AudioUnitUninitialize(unit);
    AudioComponentInstanceDispose(unit);
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
