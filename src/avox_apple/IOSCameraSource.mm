#include "IOSCameraSource.hpp"
#include "avox/module/AvoxManager.hpp"
#include "avox/module/HighClock.hpp"
#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>

// 在全局作用域声明命名空间内的类
namespace avox {
class IOSCameraSource;
}

// 独立的Objective-C委托类
@interface IOSCameraSourceDelegate
    : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate> {
@public
  avox::IOSCameraSource *cameraSource;
}
- (id)initWithCameraSource:(avox::IOSCameraSource *)source;
@end

@implementation IOSCameraSourceDelegate

- (id)initWithCameraSource:(avox::IOSCameraSource *)source {
  self = [super init];
  if (self) {
    cameraSource = source;
  }
  return self;
}

- (void)captureOutput:(AVCaptureOutput *)output
    didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
           fromConnection:(AVCaptureConnection *)connection {
  // 调用C++对象的处理方法
  if (cameraSource) {
    cameraSource->handleVideoOutput(output, sampleBuffer, connection);
  }
}

@end

namespace avox {

void regIOSCameraSource() {
  RegFunc regFunc = {"ios avfoundation camera device init", []() {
                       AvoxManager::Get().vDeviceMgr.regMgrObj(
                           VDeviceSdk::ios_avf, []() -> IVideoManager * {
                             return new IOSCameraSourceMgr();
                           });
                     }};
  AvoxManager::Get().initFuncs.push_back(regFunc);
}

// 辅助函数：请求媒体类型权限
static bool requestMediaPermission(AVMediaType mediaType) {
  __block bool permissionGranted = false;

  // 检查当前权限状态
  AVAuthorizationStatus status =
      [AVCaptureDevice authorizationStatusForMediaType:mediaType];

  if (status == AVAuthorizationStatusAuthorized) {
    // 已经授权
    permissionGranted = true;
  } else if (status == AVAuthorizationStatusNotDetermined) {
    // 尚未决定，请求权限
    dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);

    [AVCaptureDevice requestAccessForMediaType:mediaType
                             completionHandler:^(BOOL granted) {
                               permissionGranted = granted;
                               dispatch_semaphore_signal(semaphore);
                             }];

    // 等待权限请求完成，最多等待5秒
    dispatch_semaphore_wait(semaphore,
                            dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC));
    // dispatch_release(semaphore);
    semaphore = nil;
  }

  return permissionGranted;
}

IOSCameraSource::IOSCameraSource(AVCaptureDevice* device, AVCaptureSession* session, dispatch_queue_t queue)
    : captureDevice(device), captureSession(session), captureQueue(queue) {
  deviceInput = nullptr;
    videoOutput = nullptr;
    bOpen = false;
    deviceName = [[device localizedName] UTF8String];
  taskName = "ios avfoundation camera device";
  // 创建委托对象并关联当前C++对象
  captureDelegate = [[IOSCameraSourceDelegate alloc] initWithCameraSource:this];
  // 获取设备支持的所有分辨率
  for (AVCaptureDeviceFormat *format in device.formats) {
    CMVideoDimensions dimensions =
        CMVideoFormatDescriptionGetDimensions(format.formatDescription);
    // 检查是否支持NV12格式
    FourCharCode mediaSubType =
        CMFormatDescriptionGetMediaSubType(format.formatDescription);
    if (mediaSubType == kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange ||
        mediaSubType == kCVPixelFormatType_420YpCbCr8BiPlanarFullRange) {
      VideoDesc videoDesc = {};
      videoDesc.width = dimensions.width;
      videoDesc.height = dimensions.height;
      videoDesc.type = YuvType::nv12;
      descList.push_back(videoDesc);
    }
  }
  AVCaptureDevicePosition position = [device position];
  if (position == AVCaptureDevicePositionFront) {
      bBack = false;  
      setVideoDesc(1280, 720, 30);      
  } else if (position == AVCaptureDevicePositionBack) {
      bBack = true;   
      setVideoDesc(1920,1080, 30);       
  } else {
     if (!descList.empty()) {
      setVideoDesc(descList[0].width, descList[0].height, 30);
    } 
  }
}

IOSCameraSource::~IOSCameraSource() { cleanup(); }

bool IOSCameraSource::onOpen() {
  startTask();
  return true;
}

void IOSCameraSource::onRunTask() {
  if (!setupCaptureSession()) {return;}
  // 只要 Session 没跑，就启动它
  if (![captureSession isRunning]) {
      [captureSession startRunning];
  }
  bOpen = true;
  while (running()) {
      sleepTask(false, 100);
  }
  // 停止时只解绑回调，不 stopRunning 以便下一个相机秒切
  [videoOutput setSampleBufferDelegate:nil queue:nil];
  bOpen = false;
}

void IOSCameraSource::onClose() { stopTask(); }

bool IOSCameraSource::bOpening() { return bOpen; }

bool IOSCameraSource::setupCaptureSession() {
  NSError *error = nil;
  [captureSession beginConfiguration]; // 开始原子配置
  // 1. 配置 Preset
  NSString *preset = bBack ? AVCaptureSessionPreset1920x1080 : AVCaptureSessionPreset1280x720;
  if ([captureSession canSetSessionPreset:preset]) {
      [captureSession setSessionPreset:preset];
  }
  LOGFLF(LogLevel::info, "setting up camera:", deviceName.c_str(), " size:",curDesc.width, "x", curDesc.height);
  // 2. 切换 Input (这是秒切的核心)
  for (AVCaptureInput *input in [captureSession inputs]) {
      [captureSession removeInput:input];
  }
  deviceInput = [AVCaptureDeviceInput deviceInputWithDevice:captureDevice error:&error];
  if (deviceInput && [captureSession canAddInput:deviceInput]) {
      [captureSession addInput:deviceInput];
  }
  // 3. 配置 Output (重用或创建)
  if ([[captureSession outputs] count] == 0) {
      videoOutput = [[AVCaptureVideoDataOutput alloc] init];
      videoOutput.videoSettings = @{(id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange)};
      if ([captureSession canAddOutput:videoOutput]) {
          [captureSession addOutput:videoOutput];
      }
  } else {
      videoOutput = (AVCaptureVideoDataOutput *)[[captureSession outputs] firstObject];
  }
  // 4. 绑定当前实例的回调
  [videoOutput setSampleBufferDelegate:captureDelegate queue:captureQueue];
  // 提交配置，硬件会平滑切换
  [captureSession commitConfiguration]; 
  // 5. 处理镜像和方向 (解决前置预览反向问题)
  AVCaptureConnection *connection = [videoOutput connectionWithMediaType:AVMediaTypeVideo];
  if ([connection isVideoMirroringSupported]) {
      [connection setVideoMirrored:!bBack]; // 前置镜像，后置不镜像
  }
  if ([connection isVideoOrientationSupported]) {
      [connection setVideoOrientation:AVCaptureVideoOrientationPortrait];
  }
  return true;
}

void IOSCameraSource::cleanup() {
  if (captureSession) {
      [captureSession beginConfiguration];
      [captureSession removeInput:deviceInput];
      [captureSession commitConfiguration];
  }
  deviceInput = nil;
  bOpen = false;
}

void IOSCameraSource::handleVideoOutput(AVCaptureOutput *captureOutput,
                                        CMSampleBufferRef sampleBuffer,
                                        AVCaptureConnection *connection) {
  if (!sampleBuffer) {
    return;
  }
  // CMSampleBufferGetImageBuffer返回的CVImageBufferRef 是由 CMSampleBufferRef
  // 拥有的 系统处理完 sampleBuffer 后，会自动释放其包含的
  // imageBuffer,不用手动释放
  CVImageBufferRef imageBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
  if (!imageBuffer) {
    return;
  }
  // 增加引用,否则出了当前函数,imageBuffer会被MSampleBufferRef自动释放
  // CFRetain(imageBuffer); 
  // 获取时间戳
  CMTime presentationTime =
      CMSampleBufferGetPresentationTimeStamp(sampleBuffer);
  int64_t timestamp = CMTimeGetSeconds(presentationTime) * 1000;
  // 获取图像信息
  size_t width = CVPixelBufferGetWidth(imageBuffer);
  size_t height = CVPixelBufferGetHeight(imageBuffer);
  // 创建GpuFrame - 与IOSVDecoder相同的数据结构
  GpuFrame frame = {};
  frame.format.width = (int)width;
  frame.format.height = (int)height;
  frame.format.type = YuvType::nv12;
  frame.pts = timeStampMS();
  frame.dts = frame.pts;
  frame.buffer = (void *)imageBuffer;
  // 渲染处理或是编码处理
  onFrame(frame);
  // 比较麻烦,如果是直接渲染,则渲染完需要自动释放,上面不能加引用
  // 如果是编码,则需要编码完成后,手动释放,上面要加引用
  // 当前帧由CMSampleBufferRef管理，不需要手动释放
  // CFRelease(imageBuffer);
}

IOSCameraSourceMgr::IOSCameraSourceMgr() {
  availableDevices = [[NSArray alloc] init];
  onInitDevices();
}

IOSCameraSourceMgr::~IOSCameraSourceMgr() { onDeInitDevices(); }

void IOSCameraSourceMgr::onInitDevices() {
  devices.clear();
  sharedSession = [[AVCaptureSession alloc] init];
  sharedQueue = dispatch_queue_create("com.avox.ios.camera.shared", DISPATCH_QUEUE_SERIAL);
  // 请求相机权限
  bool cameraPermission = requestMediaPermission(AVMediaTypeVideo);
  if (!cameraPermission) {
    LOGFLF(LogLevel::warn, "Camera permission denied");
    return;
  }
  // 获取所有可用的视频设备
  AVCaptureDeviceDiscoverySession *discoverySession = [AVCaptureDeviceDiscoverySession
          discoverySessionWithDeviceTypes:@[AVCaptureDeviceTypeBuiltInWideAngleCamera]
          mediaType:AVMediaTypeVideo
          position:AVCaptureDevicePositionUnspecified];
    availableDevices = discoverySession.devices;

    for (AVCaptureDevice *device in availableDevices) {
        // 将共享资源注入
        std::shared_ptr<IOSCameraSource> videoPtr =
            std::make_shared<IOSCameraSource>(device, sharedSession, sharedQueue);
        devices.push_back(videoPtr);
    }
}

void IOSCameraSourceMgr::onDeInitDevices() {
    if (sharedSession) {
        [sharedSession stopRunning];
        sharedSession = nil;
    }
    sharedQueue = nullptr;
    devices.clear();
  availableDevices = nil;
}

}
