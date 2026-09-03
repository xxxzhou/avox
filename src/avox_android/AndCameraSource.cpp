#include "AndCameraSource.hpp"

#include "avox/module/AvoxManager.hpp"
#include "avox/module/HighClock.hpp"

namespace avox {

void regAndCameraSource() {
  RegFunc regFunc = {"android ndkcamera2 device init", []() {
                       AvoxManager::Get().vDeviceMgr.regMgrObj(
                           VDeviceSdk::and_ndkcamer2, []() -> IVideoManager* {
                             return new AndCameraSourceMgr();
                           });
                     }};
  AvoxManager::Get().initFuncs.push_back(regFunc);
}

// 需要添加相机设备回调结构体
static ACameraDevice_StateCallbacks cameraCallbacks = {
    .context = nullptr,
    .onDisconnected = AndCameraSource::onDisconnected,
    .onError = AndCameraSource::onError};

// 将Android camera2ndk错误码转换为字符串
static const char* cameraErrorToString(int error) {
  switch (error) {
    case ACAMERA_ERROR_CAMERA_DISCONNECTED:
      return "camera disconnected";
    case ACAMERA_ERROR_CAMERA_IN_USE:
      return "camera is in use";
    case ACAMERA_ERROR_CAMERA_DISABLED:
      return "camera is disabled";
    case ACAMERA_ERROR_CAMERA_DEVICE:
      return "camera device error";
    case ACAMERA_ERROR_CAMERA_SERVICE:
      return "camera service error";
    case ACAMERA_ERROR_INVALID_OPERATION:
      return "invalid operation";
    default:
      return "unknown camera error";
  }
}

AndCameraSource::AndCameraSource(ACameraManager* cameraManager,
                                 const char* id) {
  manager = cameraManager;
  deviceId = id;
  deviceName = deviceId;
  deviceKind = VDeviceKind::camera;
  // 获取相机设备的特性
  ACameraMetadata* metadata = nullptr;
  ACameraManager_getCameraCharacteristics(cameraManager, id, &metadata);
  ACameraMetadata_const_entry entry = {};
  ACameraMetadata_getConstEntry(
      metadata, ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, &entry);
  for (int i = 0; i < entry.count; i += 4) {
    int32_t input = entry.data.i32[i + 3];
    int32_t format = entry.data.i32[i + 0];
    if (input) {
      continue;
    }
    VideoDesc videoDesc = {};
    videoDesc.width = entry.data.i32[i + 1];
    videoDesc.height = entry.data.i32[i + 2];
    // AIMAGE_FORMAT_YUV_420_888 可能是NV12，也可能是YUV420P
    switch (format) {
      case AIMAGE_FORMAT_YUV_420_888: {
        videoDesc.type = YuvType::nv12;
        break;
      }
      default: {
        videoDesc.type = YuvType::other;
        break;
      }
    }
    if (videoDesc.type != YuvType::other) {
      descList.push_back(videoDesc);
    }
  }
  // 查找前后置
  camera_status_t status =
      ACameraMetadata_getConstEntry(metadata, ACAMERA_LENS_FACING, &entry);
  if (status == ACAMERA_OK) {
    auto facing = static_cast<acamera_metadata_enum_android_lens_facing_t>(
        entry.data.u8[0]);
    bBack = facing == ACAMERA_LENS_FACING_BACK;
    if (facing == ACAMERA_LENS_FACING_BACK) {
      deviceName = "Back Camera (" + std::string(id) + ")";
    } else if (facing == ACAMERA_LENS_FACING_FRONT) {
      deviceName = "Front Camera (" + std::string(id) + ")";
    } else {
      deviceName = "External/Unknown Camera (" + std::string(id) + ")";
    }
  }
  if (metadata != nullptr) {
    ACameraMetadata_free(metadata);
  }
  taskName = "android ndkcamera2";
  if (bBack) {
    setVideoDesc(1920, 1080, 30);
  } else {
    setVideoDesc(1280, 720, 30);
  }
}

AndCameraSource::~AndCameraSource() {}

bool AndCameraSource::onOpen() {
  startTask();
  return true;
}

void AndCameraSource::onRunTask() {
  // 打开EGL环境，并创建SurfaceTexture对象，并绑定到EGL环境中
  bool bOpen = openSurfaceTexture();
  if (bOpen) {
    // 打开相机设备，创建CaptureSession
    bOpen = createCaptureSession();
  }
  std::mutex frameMutex;
  if (bOpen) {
    surfaceTexture->setOnFrameAvailableListener([this]() {
      this->bFrameAvailable = true;
      this->frameCv.notify_one();
    });
    while (running()) {
      // 等待surfaceTexture可用
      std::unique_lock<std::mutex> lock(frameMutex);
      // HighClock clock = {};
      auto notified = frameCv.wait_for(
          lock, std::chrono::milliseconds(200),
          [this] { return this->bFrameAvailable.load() || !running(); });
      // 如果线程已经关闭,直接退出
      if (!running()) {
        break;
      }
      if (notified) {
        bFrameAvailable = false;  // 重置标志
        surfaceTexture->updateTexImage();
      } else {
        LOGFLF(LogLevel::info, "time out");
        // 如果开启推流,这里会超时一下,需要让surfaceTexture继续更新
        // surfaceTexture->updateTexImage();
        continue;
      }
      // 填充GpuFrame
      GpuFrame frame = {};
      frame.format.width = curDesc.width;
      frame.format.height = curDesc.height;
      frame.format.type = YuvType::nv12;
      frame.pts = timeStampMS();
      frame.dts = frame.pts;
      frame.context = this;
      onFrame(frame); 
      // LOGFLF(LogLevel::info, "pts:", frame.pts);
    }
    surfaceTexture->setOnFrameAvailableListener(nullptr);
  } else {
    LOGFLF(LogLevel::warn, "open camera failed");
  }
  cleanup();
}

void AndCameraSource::onClose() {
  frameCv.notify_all();
  stopTask();
}

bool AndCameraSource::bOpening() { return bOpen; }

bool AndCameraSource::openSurfaceTexture() {
  nativeWindow = nullptr;
  // 初始化EGL环境
  initContext(EGL_NO_CONTEXT);
  if (eglGetCurrentContext()) {
    surfaceTexture = std::make_unique<JniSurfaceTexture>();
    // 从GPU队列取出来的存放的OES纹理
    glGenTextures(1, &textureId);
    AVOX_GL_LOG("gen texture failed.");
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, textureId);
    glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S,
                    GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T,
                    GL_CLAMP_TO_EDGE);
    surfaceTexture->init(textureId);
    surfaceTexture->setImageSize(curDesc.width, curDesc.height);
    nativeWindow = surfaceTexture->getNativeWindow();
    LOGFLF(LogLevel::info, "textureId:", textureId, " width:", curDesc.width,
           " height:", curDesc.height, " nativeWindow:", nativeWindow);
    return true;
  }
  return false;
}

bool AndCameraSource::createCaptureSession() {
  // 打开相机设备
  camera_status_t status = ACameraManager_openCamera(
      manager, deviceId.c_str(), &cameraCallbacks, &cameraDevice);
  if (status != ACAMERA_OK) {
    LOGFLF(LogLevel::warn, "failed to open camera device:", status);
    return false;
  }
  // 创建输出容器
  status = ACaptureSessionOutputContainer_create(&outputContainer);
  if (status != ACAMERA_OK) {
    LOGFLF(LogLevel::warn, "failed to create output container:", status);
    return false;
  }
  // 创建会话输出
  status = ACaptureSessionOutput_create(nativeWindow, &sessionOutput);
  if (status != ACAMERA_OK) {
    LOGFLF(LogLevel::warn, "failed to create session output:", status);
    return false;
  }
  // 添加输出到容器
  status = ACaptureSessionOutputContainer_add(outputContainer, sessionOutput);
  if (status != ACAMERA_OK) {
    LOGFLF(LogLevel::warn, "failed to add output to container:", status);
    return false;
  }
  // 创建捕获请求
  status = ACameraDevice_createCaptureRequest(cameraDevice, TEMPLATE_PREVIEW,
                                              &captureRequest);
  if (status != ACAMERA_OK) {
    LOGFLF(LogLevel::warn, "failed to create capture request:", status);
    return false;
  }
  // 先创建ACameraOutputTarget，再添加到捕获请求
  ACameraOutputTarget* outputTarget = nullptr;
  status = ACameraOutputTarget_create(nativeWindow, &outputTarget);
  if (status != ACAMERA_OK) {
    LOGFLF(LogLevel::warn, "failed to create camera output target:", status);
    return false;
  }
  status = ACaptureRequest_addTarget(captureRequest, outputTarget);
  if (status != ACAMERA_OK) {
    LOGFLF(LogLevel::warn, "failed to add target to capture request:", status);
    ACameraOutputTarget_free(outputTarget);
    return false;
  }
  // 创建捕获会话
  ACameraCaptureSession_stateCallbacks sessionCallbacks = {
      .context = this,
      .onClosed = onSessionClosed,
      .onReady = onSessionReady,
      .onActive = onSessionActive};
  status = ACameraDevice_createCaptureSession(
      cameraDevice, outputContainer, &sessionCallbacks, &captureSession);
  if (status != ACAMERA_OK) {
    LOGFLF(LogLevel::warn, "failed to create capture session:", status);
    return false;
  }
  // 设置重复请求
  ACaptureRequest** requests = &captureRequest;
  status = ACameraCaptureSession_setRepeatingRequest(captureSession, nullptr, 1,
                                                     requests, nullptr);
  if (status != ACAMERA_OK) {
    LOGFLF(LogLevel::warn, "failed to set repeating request:", status);
    ACameraOutputTarget_free(outputTarget);
    return false;
  }
  // 释放outputTarget，因为captureRequest已经持有它的引用
  ACameraOutputTarget_free(outputTarget);
  LOGFLF(LogLevel::info, "capture session created successfully");
  return true;
}

void AndCameraSource::cleanup() {
  // 停止捕获会话
  if (captureSession) {
    ACameraCaptureSession_stopRepeating(captureSession);
    ACameraCaptureSession_close(captureSession);
    captureSession = nullptr;
  }
  // 释放资源
  if (captureRequest) {
    ACaptureRequest_free(captureRequest);
    captureRequest = nullptr;
  }
  if (sessionOutput) {
    ACaptureSessionOutput_free(sessionOutput);
    sessionOutput = nullptr;
  }
  if (outputContainer) {
    ACaptureSessionOutputContainer_free(outputContainer);
    outputContainer = nullptr;
  }
  if (cameraDevice) {
    ACameraDevice_close(cameraDevice);
    cameraDevice = nullptr;
  }
  // 释放SurfaceTexture资源
  if (surfaceTexture) {
    surfaceTexture->close();
    surfaceTexture.reset();
  }
  // 删除纹理
  if (textureId != 0) {
    glDeleteTextures(1, &textureId);
    textureId = 0;
  }
  // 释放EGL环境
  unInit();
}

// 相机设备回调
void AndCameraSource::onDisconnected(void* context, ACameraDevice* device) {
  AndCameraSource* source = static_cast<AndCameraSource*>(context);
  source->lostDevice();
  LOGFLF(LogLevel::warn, "Camera disconnected");
}

void AndCameraSource::onError(void* context, ACameraDevice* device, int error) {
  AndCameraSource* source = static_cast<AndCameraSource*>(context);
  source->onError(error);
  LOGFLF(LogLevel::warn, "Camera error:", error);
}

// 会话回调
void AndCameraSource::onSessionClosed(void* context,
                                      ACameraCaptureSession* session) {
  LOGFLF(LogLevel::info, "Capture session closed");
  AndCameraSource* source = static_cast<AndCameraSource*>(context);
  source->bOpen = false;
}

void AndCameraSource::onSessionReady(void* context,
                                     ACameraCaptureSession* session) {
  LOGFLF(LogLevel::info, "Capture session ready");
}

void AndCameraSource::onSessionActive(void* context,
                                      ACameraCaptureSession* session) {
  LOGFLF(LogLevel::info, "Capture session active");
  AndCameraSource* source = static_cast<AndCameraSource*>(context);
  source->bOpen = true;
}

void AndCameraSource::lostDevice() {
  dispatch(&IVideoSourceOb::onVideoError, AVError::devcieLost, "device lost");
}

void AndCameraSource::onError(int error) {
  AVError avError = AVError::none;
  switch (error) {
    case ACAMERA_ERROR_CAMERA_DISCONNECTED:
      avError = AVError::devcieLost;
      break;
    case ACAMERA_ERROR_CAMERA_IN_USE:
      avError = AVError::deviceBusy;
      break;
    case ACAMERA_ERROR_CAMERA_DISABLED:
      avError = AVError::deviceDisable;
      break;
    default:
      avError = AVError::deviceError;
  }
  // error 转为特定字符串
  const char* errorStr = cameraErrorToString(error);
  dispatch(&IVideoSourceOb::onVideoError, avError, errorStr);
}

AndCameraSourceMgr::AndCameraSourceMgr() {
  cameraManager = ACameraManager_create();
  onInitDevices();
}

AndCameraSourceMgr::~AndCameraSourceMgr() {
  if (cameraIdList != nullptr) {
    ACameraManager_deleteCameraIdList(cameraIdList);
    cameraIdList = nullptr;
  }
  if (cameraManager) {
    ACameraManager_delete(cameraManager);
    cameraManager = nullptr;
  }
}

void AndCameraSourceMgr::onInitDevices() {
  devices.clear();
  ACameraManager_getCameraIdList(cameraManager, &cameraIdList);
  for (int32_t index = 0; index < cameraIdList->numCameras; index++) {
    std::shared_ptr<AndCameraSource> videoPtr =
        std::make_shared<AndCameraSource>(cameraManager,
                                          cameraIdList->cameraIds[index]);
    devices.push_back(videoPtr);
  }
}

void AndCameraSourceMgr::onDeInitDevices() {}

}
