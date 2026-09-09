#include "IOSAudioSource.hpp"

#include "avox/module/AvoxManager.hpp"
#include "avox/module/HighClock.hpp"
#import <AVFoundation/AVFoundation.h>
#import <CoreAudio/CoreAudioTypes.h>

namespace avox {
class IOSAudioSource;
}

// 独立的Objective-C委托类
@interface IOSAudioSourceDelegate
    : NSObject <AVCaptureAudioDataOutputSampleBufferDelegate> {
@public
  avox::IOSAudioSource* audioSource;
}
- (id)initWithAudioSource:(avox::IOSAudioSource*)source;
@end

@implementation IOSAudioSourceDelegate

- (id)initWithAudioSource:(avox::IOSAudioSource*)source {
  self = [super init];
  if (self) {
    audioSource = source;
  }
  return self;
}

- (void)captureOutput:(AVCaptureOutput*)output
    didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
           fromConnection:(AVCaptureConnection*)connection {
  if (audioSource) {
    audioSource->handleAudioOutput(output, sampleBuffer, connection);
  }
}

@end

namespace avox {

void regIOSAudioSource() {
  RegFunc regFunc = {"ios avfoundation audio device init", []() {
                       AvoxManager::Get().aDeviceMgr.regMgrObj(
                           ADeviceSdk::ios, []() -> IAudioManager* {
                             return new IOSAudioSourceMgr();
                           });
                     }};
  AvoxManager::Get().initFuncs.push_back(regFunc);
}

// 辅助函数：请求媒体类型权限
static bool requestMediaPermission(AVMediaType mediaType) {
  __block bool permissionGranted = false;

  AVAuthorizationStatus status =
      [AVCaptureDevice authorizationStatusForMediaType:mediaType];

  if (status == AVAuthorizationStatusAuthorized) {
    permissionGranted = true;
  } else if (status == AVAuthorizationStatusNotDetermined) {
    dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);

    [AVCaptureDevice requestAccessForMediaType:mediaType
                             completionHandler:^(BOOL granted) {
                               permissionGranted = granted;
                               dispatch_semaphore_signal(semaphore);
                             }];

    dispatch_semaphore_wait(semaphore,
                            dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC));
  }

  return permissionGranted;
}

IOSAudioSource::IOSAudioSource(AVCaptureDevice* device,
                               AVCaptureSession* session,
                               dispatch_queue_t queue)
    : captureDevice(device),
      captureSession(session),
      captureQueue(queue) {
  deviceInput = nullptr;
  audioOutput = nullptr;
  bOpen = false;
  deviceName = [[device localizedName] UTF8String];
  taskName = "ios avfoundation audio device";
  captureDelegate = [[IOSAudioSourceDelegate alloc] initWithAudioSource:this];

  // 获取设备支持的音频格式
  for (AVCaptureDeviceFormat* format in device.formats) {
    CMFormatDescriptionRef formatDesc = format.formatDescription;
    const AudioStreamBasicDescription* asbd =
        CMAudioFormatDescriptionGetStreamBasicDescription(formatDesc);
    if (asbd) {
      desc.sampleRate = asbd->mSampleRate;
      desc.channels = asbd->mChannelsPerFrame;
      // iOS通常使用Float32格式
      if (asbd->mFormatFlags & kAudioFormatFlagIsFloat) {
        desc.format = AudioFormat::AVOX_AUDIO_FLT;
      } else if (asbd->mFormatFlags & kAudioFormatFlagIsSignedInteger) {
        if (asbd->mBitsPerChannel == 16) {
          desc.format = AudioFormat::AVOX_AUDIO_S16;
        } else if (asbd->mBitsPerChannel == 32) {
          desc.format = AudioFormat::AVOX_AUDIO_S32;
        }
      }
      break;
    }
  }
}

IOSAudioSource::~IOSAudioSource() { cleanup(); }

bool IOSAudioSource::onOpen() {
  startTask();
  return true;
}

void IOSAudioSource::onRunTask() {
  if (!setupCaptureSession()) {
    return;
  }
  if (![captureSession isRunning]) {
    [captureSession startRunning];
  }
  bOpen = true;
  while (running()) {
    sleepTask(false, 100);
  }
  [audioOutput setSampleBufferDelegate:nil queue:nil];
  bOpen = false;
}

bool IOSAudioSource::bOpening() { return bOpen; }

bool IOSAudioSource::setupCaptureSession() {
  NSError* error = nil;
  [captureSession beginConfiguration];

  // 移除旧的输入
  for (AVCaptureInput* input in [captureSession inputs]) {
    [captureSession removeInput:input];
  }

  // 添加音频输入
  deviceInput = [AVCaptureDeviceInput deviceInputWithDevice:captureDevice
                                                      error:&error];
  if (deviceInput && [captureSession canAddInput:deviceInput]) {
    [captureSession addInput:deviceInput];
  } else {
    LOGFLF(LogLevel::warn, "Failed to add audio input");
    [captureSession commitConfiguration];
    return false;
  }

  // 配置音频输出
  if ([[captureSession outputs] count] == 0) {
    audioOutput = [[AVCaptureAudioDataOutput alloc] init];
    if ([captureSession canAddOutput:audioOutput]) {
      [captureSession addOutput:audioOutput];
    }
  } else {
    audioOutput = (AVCaptureAudioDataOutput*)[[captureSession outputs] firstObject];
  }

  [audioOutput setSampleBufferDelegate:captureDelegate queue:captureQueue];

  [captureSession commitConfiguration];

  LOGFLF(LogLevel::info, "iOS audio device setup success:", deviceName.c_str(),
         " desc: ", desc);
  return true;
}

void IOSAudioSource::cleanup() {
  if (captureSession) {
    [captureSession beginConfiguration];
    [captureSession removeInput:deviceInput];
    [captureSession commitConfiguration];
  }
  deviceInput = nil;
  bOpen = false;
}

void IOSAudioSource::handleAudioOutput(AVCaptureOutput* captureOutput,
                                       CMSampleBufferRef sampleBuffer,
                                       AVCaptureConnection* connection) {
  if (!sampleBuffer) {
    return;
  }
  CMBlockBufferRef blockBuffer = CMSampleBufferGetDataBuffer(sampleBuffer);
  if (!blockBuffer) {
    return;
  }
  size_t length = CMBlockBufferGetDataLength(blockBuffer);
  if (length == 0) {
    return;
  }
  // 获取音频格式信息
  CMFormatDescriptionRef formatDesc = CMSampleBufferGetFormatDescription(sampleBuffer);
  const AudioStreamBasicDescription* asbd =
      CMAudioFormatDescriptionGetStreamBasicDescription(formatDesc);
  // 临时缓冲区
  std::vector<uint8_t> buffer(length);
  // 复制音频数据
  CMBlockBufferCopyDataBytes(blockBuffer, 0, length, buffer.data());
  // 更新音频描述
  if (asbd && !bFirstFrame) {
    desc.sampleRate = asbd->mSampleRate;
    desc.channels = asbd->mChannelsPerFrame;
    if (asbd->mFormatFlags & kAudioFormatFlagIsFloat) {
      desc.format = AudioFormat::AVOX_AUDIO_FLT;
    } else if (asbd->mFormatFlags & kAudioFormatFlagIsSignedInteger) {
      if (asbd->mBitsPerChannel == 16) {
        desc.format = AudioFormat::AVOX_AUDIO_S16;
      } else if (asbd->mBitsPerChannel == 32) {
        desc.format = AudioFormat::AVOX_AUDIO_S32;
      }
    }
  }
  // 创建音频帧
  AvoxAFrame frame = {};
  frame.buffer.data = buffer.data();
  frame.buffer.size = length;
  frame.pts = timeStampMS();
  onFrame(frame);
}

void IOSAudioSource::onClose() { stopTask(); }

IOSAudioSourceMgr::IOSAudioSourceMgr() {
  availableDevices = [[NSArray alloc] init];
  onInitDevices();
}

IOSAudioSourceMgr::~IOSAudioSourceMgr() { onDeInitDevices(); }

void IOSAudioSourceMgr::onInitDevices() {
  devices.clear();
  sharedSession = [[AVCaptureSession alloc] init];
  sharedQueue = dispatch_queue_create("com.avox.ios.audio.shared",
                                       DISPATCH_QUEUE_SERIAL);

  // 请求麦克风权限
  bool micPermission = requestMediaPermission(AVMediaTypeAudio);
  if (!micPermission) {
    LOGFLF(LogLevel::warn, "Microphone permission denied");
    return;
  }
  // 获取所有可用的音频设备
  AVCaptureDeviceDiscoverySession* discoverySession =
      [AVCaptureDeviceDiscoverySession
          discoverySessionWithDeviceTypes:@[AVCaptureDeviceTypeBuiltInMicrophone]
                               mediaType:AVMediaTypeAudio
                                position:AVCaptureDevicePositionUnspecified];
  availableDevices = discoverySession.devices;

  for (AVCaptureDevice* device in availableDevices) {
    std::shared_ptr<IOSAudioSource> audioPtr =
        std::make_shared<IOSAudioSource>(device, sharedSession, sharedQueue);
    devices.push_back(audioPtr);
  }

  LOGFLF(LogLevel::info, "iOS found", devices.size(), "audio devices");
}

void IOSAudioSourceMgr::onDeInitDevices() {
  if (sharedSession) {
    [sharedSession stopRunning];
    sharedSession = nil;
  }
  sharedQueue = nullptr;
  devices.clear();
  availableDevices = nil;
}

}
