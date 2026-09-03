#include "IOSAudioRender.hpp"

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

  // 设置音频会话
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

#ifdef AVOX_ENABLE_FFMPEG
  if (!resample->init(desc, renderDesc)) {
    LOGFLF(LogLevel::warn, "resample init failed");
  }
#endif

  // 创建 Audio Unit
  AudioComponentDescription compDesc = {};
  compDesc.componentType = kAudioUnitType_Output;
  compDesc.componentSubType = kAudioUnitSubType_RemoteIO;
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
  std::unique_lock<std::mutex> lock(self->mtx);

  if (!self->audioUnit || !ioData) {
    return noErr;
  }

  uint32_t sampleSize = self->renderDesc.channels * audioFormatSize(self->renderDesc.format);
  uint32_t bytesNeeded = inNumberFrames * sampleSize;

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
      // 缓冲区数据不足，填充静音
      memset(outData, 0, bytesToCopy);
    }
  }

  return noErr;
}

void IOSAudioRender::onRender(const AvoxData& frame) {
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
  }
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
  std::unique_lock<std::mutex> lock(mtx);
  if (!audioUnit) {
    return;
  }

  if (pause) {
    AudioOutputUnitStop(audioUnit);
  } else {
    AudioOutputUnitStart(audioUnit);
  }
}

void IOSAudioRender::flush() {
  std::unique_lock<std::mutex> lock(mtx);
  writePos = 0;
  readPos = 0;
  totalFrames = 0;
  playedFrames = 0;
  lastQueueMs = 0;
}

void IOSAudioRender::onClose() {
  std::unique_lock<std::mutex> lock(mtx);
  if (audioUnit) {
    AudioOutputUnitStop(audioUnit);
    AudioUnitUninitialize(audioUnit);
    AudioComponentInstanceDispose(audioUnit);
    audioUnit = nullptr;
  }

  AVAudioSession* session = [AVAudioSession sharedInstance];
  [session setActive:NO error:nil];

  ringBuffer.clear();
  log(LogLevel::info, "iOS AudioUnit render close");
}

void IOSAudioRender::speed(double speed) {
  // iOS AudioUnit 不直接支持变速，需要在应用层处理
}

void IOSAudioRender::setVolume(float cvolume) {
  std::unique_lock<std::mutex> lock(mtx);
  volume = std::max(0.0f, std::min(1.0f, cvolume));

  if (audioUnit) {
    AudioUnitSetParameter(audioUnit, kHALOutputParam_Volume,
                          kAudioUnitScope_Global, 0, volume, 0);
  }
}

float IOSAudioRender::getVolume() {
  return volume;
}

}
