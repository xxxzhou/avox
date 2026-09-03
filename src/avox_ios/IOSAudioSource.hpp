#pragma once

#include <AVFoundation/AVFoundation.h>

#include "avox/module/RunTask.hpp"
#include "avox/source/AudioSource.hpp"

namespace avox {

/// iOS音频源类
class IOSAudioSource : public RunTask, public AudioSource {
 public:
  IOSAudioSource(AVCaptureDevice* device, AVCaptureSession* session, dispatch_queue_t queue);
  virtual ~IOSAudioSource();

 private:
  AVCaptureDevice* captureDevice;
  AVCaptureSession* captureSession;
  AVCaptureDeviceInput* deviceInput;
  AVCaptureAudioDataOutput* audioOutput;
  dispatch_queue_t captureQueue;
  id<AVCaptureAudioDataOutputSampleBufferDelegate> captureDelegate;
  bool bOpen = false;

 public:
  virtual bool onOpen() override;
  virtual void onClose() override;
  virtual bool bOpening() override;

 protected:
  virtual void onRunTask() override;

 private:
  bool setupCaptureSession();
  void cleanup();

 public:
  void handleAudioOutput(AVCaptureOutput* captureOutput,
                         CMSampleBufferRef sampleBuffer,
                         AVCaptureConnection* connection);
};

/// iOS音频源管理器类
class IOSAudioSourceMgr : public AudioManager<IOSAudioSource> {
 public:
  IOSAudioSourceMgr();
  virtual ~IOSAudioSourceMgr();

 protected:
  void onInitDevices() override;
  void onDeInitDevices() override;

 private:
  NSArray<AVCaptureDevice*>* availableDevices;
  AVCaptureSession* sharedSession;
  dispatch_queue_t sharedQueue;
};

}
