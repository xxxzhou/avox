#pragma once

#include "../avox/source/VideoSource.hpp"
#include "JniSurfaceTexture.hpp"
#include "avox_egl/GLESContext.hpp"

#include <camera/NdkCameraDevice.h>
#include <camera/NdkCameraManager.h>
#include <camera/NdkCameraMetadata.h>
#include <media/NdkImageReader.h>

namespace avox {
class AndCameraSource : public VideoSource, public GLESContext, public RunTask {
public:
  AndCameraSource(ACameraManager *cameraManager, const char *id);
  virtual ~AndCameraSource();

private:
  // 由AndCameraSourceMgr提供，本身只做引用，不负责释放
  ACameraManager *manager = nullptr;
  // 用于相机截获帧输出的surface
  std::unique_ptr<JniSurfaceTexture> surfaceTexture = nullptr;
  // surfaceTexture对应的ANativeWindow
  ANativeWindow *nativeWindow = nullptr;  
  // NDK相机设备
  ACameraDevice *cameraDevice = nullptr;
  // 输出容器
  ACaptureSessionOutputContainer *outputContainer = nullptr;
  // 输出会话
  ACaptureSessionOutput *sessionOutput = nullptr;
  // 捕获请求
  ACaptureRequest *captureRequest = nullptr;
  // 捕获会话
  ACameraCaptureSession *captureSession = nullptr;
  // 图像到来信号
  std::condition_variable frameCv;
  std::atomic<bool> bFrameAvailable = false;

public:
  void lostDevice();
  void onError(int error);

protected:
  virtual bool onOpen() override;
  virtual void onClose() override;
  virtual bool bOpening() override;

protected:
  virtual void onRunTask() override;

private:
  bool openSurfaceTexture();
  bool createCaptureSession();
  void cleanup();

public:
  // 回调函数
  static void onDisconnected(void *context, ACameraDevice *device);
  static void onError(void *context, ACameraDevice *device, int error);
  static void onSessionClosed(void *context, ACameraCaptureSession *session);
  static void onSessionReady(void *context, ACameraCaptureSession *session);
  static void onSessionActive(void *context, ACameraCaptureSession *session);
};

class AndCameraSourceMgr : public VideoManager<AndCameraSource> {
public:
  AndCameraSourceMgr();
  virtual ~AndCameraSourceMgr();

private:
  ACameraManager *cameraManager = nullptr;
  ACameraIdList *cameraIdList = nullptr;

protected:
  virtual void onInitDevices() override;
  virtual void onDeInitDevices() override;
};

}