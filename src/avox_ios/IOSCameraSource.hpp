#pragma once

#include <AVFoundation/AVFoundation.h>

#include "avox/module/RunTask.hpp"
#include "avox/source/VideoSource.hpp"

namespace avox {

/// iOS相机源类
class IOSCameraSource : public RunTask, public VideoSource {
 public:
  IOSCameraSource(AVCaptureDevice* device, AVCaptureSession* session, dispatch_queue_t queue);
  virtual ~IOSCameraSource();

 private:
  AVCaptureDevice* captureDevice;
  AVCaptureSession* captureSession;
  AVCaptureDeviceInput* deviceInput;
  AVCaptureVideoDataOutput* videoOutput;
  dispatch_queue_t captureQueue;
  id<AVCaptureVideoDataOutputSampleBufferDelegate> captureDelegate;

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
  void handleVideoOutput(AVCaptureOutput* captureOutput,
                         CMSampleBufferRef sampleBuffer,
                         AVCaptureConnection* connection);


};

/// iOS相机源管理器类
class IOSCameraSourceMgr : public VideoManager<IOSCameraSource> {
 public:
  IOSCameraSourceMgr();
  virtual ~IOSCameraSourceMgr();

 protected:
  void onInitDevices() override;
  void onDeInitDevices() override;

 private:
  NSArray<AVCaptureDevice*>* availableDevices;
  // 共享会话
  AVCaptureSession* sharedSession; 
  // 共享回调队列   
  dispatch_queue_t sharedQueue;      
};

}

