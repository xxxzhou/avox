#import "MacCapture.hpp"

#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <CoreVideo/CoreVideo.h>

#include <algorithm>

#include "avox/module/AvoxManager.hpp"
#include "avox/module/LogHelper.hpp"
#include "avox/source/VideoSource.hpp"

// SCK 回调桥: ObjC 声明只能在全局域(不能进 C++ namespace);
// owner 生命周期由持有 stream 的 MacCapture 保证(先 onClose 析构 stream)
@interface MacStreamOutputBridge : NSObject <SCStreamOutput>
- (instancetype)initWithOwner:(avox::MacCapture*)owner_;
- (void)stream:(SCStream*)stream
    didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
                   ofType:(SCStreamOutputType)type API_AVAILABLE(macos(12.3));
@end

@implementation MacStreamOutputBridge {
  avox::MacCapture* owner;  // 裸指针: stream 归 owner 持有, 先 onClose 析构
}

- (instancetype)initWithOwner:(avox::MacCapture*)owner_ {
  self = [super init];
  if (self) {
    owner = owner_;
  }
  return self;
}

- (void)stream:(SCStream*)stream
    didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
                   ofType:(SCStreamOutputType)type API_AVAILABLE(macos(12.3)) {
  if (type != SCStreamOutputTypeScreen || !sampleBuffer) {
    return;
  }
  CVPixelBufferRef pb = CMSampleBufferGetImageBuffer(sampleBuffer);
  if (!pb) {
    return;
  }
  if (avox::MacCapture* o = owner) {
    o->onScreenFrame((void*)pb);
  }
}

@end

namespace avox {

void MacCaptureBase::setDisplay(CGDirectDisplayID display, int32_t index) {
  displayId = display;
  displayIndex = index;
  char id[32] = {};
  snprintf(id, sizeof(id), "DISPLAY%u", (unsigned)display);
  deviceId = id;
  deviceName = "monitor " + std::to_string(index);
  deviceKind = VDeviceKind::monitor;
}

void MacCaptureBase::onScreenFrame(void* pixelBuffer) {
  // 单槽交接: 新帧让位旧帧(节拍线程持 lastEmitted, 这里只保最新)
  std::lock_guard<std::mutex> lock(frameMtx);
  if (pendingBuffer) {
    CFRelease((CFTypeRef)pendingBuffer);
  }
  CFRetain((CFTypeRef)pixelBuffer);
  pendingBuffer = pixelBuffer;
}

// ============== 采集器 ==============

bool MacCapture::onOpen() {
  if (!displayId) {
    return false;
  }
  if (@available(macOS 12.3, *)) {
    // SCK 各步异步 API 用信号量收成同步(open 调用线程可接受数百毫秒)
    dispatch_semaphore_t sem = dispatch_semaphore_create(0);
    __block SCShareableContent* content = nil;
    __block NSError* contentErr = nil;
    [SCShareableContent getShareableContentWithCompletionHandler:^(
                            SCShareableContent* c, NSError* err) {
      content = c;
      contentErr = err;
      dispatch_semaphore_signal(sem);
    }];
    dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, 5LL * NSEC_PER_SEC));
    if (!content) {
      LOGFLF(LogLevel::error, "mac capture shareable content failed:",
             contentErr ? contentErr.localizedDescription.UTF8String
                        : "screen recording permission?");
      dispatch(&IVideoSourceOb::onVideoError, AVError::deviceError,
               "screen recording permission required");
      return false;
    }
    SCDisplay* target = nil;
    for (SCDisplay* d in content.displays) {
      if (d.displayID == displayId) {
        target = d;
        break;
      }
    }
    if (!target) {
      LOGFLF(LogLevel::error, "mac capture display gone:", (int32_t)displayId);
      dispatch(&IVideoSourceOb::onVideoError, AVError::deviceError,
               "display not found");
      return false;
    }
    // 采集参数: 缺省=30fps/带光标/整屏(与 Windows 缺省口径一致)
    double fps = (bCaptureParamSet && captureParam.fps > 0) ? captureParam.fps
                                                            : 30.0;
    // 区域: 虚拟屏绝对物理像素 → 本显示器内点矩形(SCK sourceRect 按点)
    CGRect bounds = CGDisplayBounds(displayId);
    int32_t physW = (int32_t)CGDisplayPixelsWide(displayId);
    double scale = bounds.size.width > 0
                       ? (double)physW / bounds.size.width
                       : 1.0;
    double lx = 0.0;
    double ly = 0.0;
    double lw = bounds.size.width;
    double lh = bounds.size.height;
    bool bRegion = false;
    int32_t repX = 0;
    int32_t repY = 0;
    int32_t repW = 0;
    int32_t repH = 0;
    if (bCaptureParamSet && captureParam.regionW > 1 &&
        captureParam.regionH > 1) {
      double rx = captureParam.regionX / scale;
      double ry = captureParam.regionY / scale;
      double rr = (captureParam.regionX + captureParam.regionW) / scale;
      double rb = (captureParam.regionY + captureParam.regionH) / scale;
      lx = std::max(rx, bounds.origin.x) - bounds.origin.x;
      ly = std::max(ry, bounds.origin.y) - bounds.origin.y;
      double rRight = std::min(rr, bounds.origin.x + bounds.size.width);
      double rBottom = std::min(rb, bounds.origin.y + bounds.size.height);
      lw = rRight - (bounds.origin.x + lx);
      lh = rBottom - (bounds.origin.y + ly);
      if (lw >= 1.0 && lh >= 1.0) {
        bRegion = true;
      } else {
        LOGFLF(LogLevel::warn, "mac capture region off-display, fallback full");
        lx = 0.0;
        ly = 0.0;
        lw = bounds.size.width;
        lh = bounds.size.height;
        bRegion = false;
      }
    }
    int32_t outW = (int32_t)(lw * scale) & ~1;
    int32_t outH = (int32_t)(lh * scale) & ~1;
    if (outW < 2 || outH < 2) {
      dispatch(&IVideoSourceOb::onVideoError, AVError::dataNoVaild,
               "capture size invalid");
      return false;
    }
    if (bRegion) {
      // 回报实际生效矩形(物理, 虚拟屏绝对, 偶对齐)
      repX = (int32_t)((bounds.origin.x + lx) * scale) & ~1;
      repY = (int32_t)((bounds.origin.y + ly) * scale) & ~1;
      repW = outW;
      repH = outH;
    }
    SCStreamConfiguration* config = [[SCStreamConfiguration alloc] init];
    config.pixelFormat = kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
    config.width = outW;
    config.height = outH;
    config.sourceRect = CGRectMake(bounds.origin.x + lx, bounds.origin.y + ly,
                                   lw, lh);
    config.minimumFrameInterval = CMTimeMake(1, (int32_t)(fps + 0.5));
    config.queueDepth = 3;
    // 光标: SCK 可关(12.3+), 关掉时光标只存在于宿主遥测
    config.showsCursor = bCaptureParamSet ? captureParam.captureCursor : true;
    config.capturesAudio = NO;
    cursorMode = (bCaptureParamSet && !captureParam.captureCursor)
                     ? "disabled"
                     : "baked";
    SCContentFilter* filter =
        [[SCContentFilter alloc] initWithDisplay:target excludingWindows:@[]];
    NSError* err = nil;
    SCStream* s = [[SCStream alloc] initWithFilter:filter
                                     configuration:config
                                          delegate:nil];
    streamOutput = (__bridge_retained void*)[[MacStreamOutputBridge alloc] initWithOwner:this];
    outputQueue = (__bridge void*)dispatch_queue_create("avox.mac.capture", DISPATCH_QUEUE_SERIAL);
    NSError* addErr = nil;
    if (![s addStreamOutput:(id<SCStreamOutput>)(__bridge id)streamOutput
                       type:SCStreamOutputTypeScreen
         sampleHandlerQueue:(__bridge dispatch_queue_t)outputQueue
                      error:&addErr] ||
        addErr) {
      LOGFLF(LogLevel::error, "mac capture addStreamOutput failed:",
             addErr ? addErr.localizedDescription.UTF8String : "?");
      // 归还 __bridge_retained 的 +1(与 onClose 同法): 不还则下一次 open
      // 覆盖成员时直接漏掉
      {
        id obj = (__bridge_transfer id)streamOutput;
        obj = nil;
      }
      streamOutput = nullptr;
      outputQueue = nullptr;  // queue 单实例泄漏量级可忽略(同 onClose)
      dispatch(&IVideoSourceOb::onVideoError, AVError::deviceError,
               "add stream output failed");
      return false;
    }
    __block BOOL started = NO;
    __block NSError* startErr = nil;
    [s startCaptureWithCompletionHandler:^(NSError* e) {
      started = e == nil;
      startErr = e;
      dispatch_semaphore_signal(sem);
    }];
    dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, 5LL * NSEC_PER_SEC));
    if (!started) {
      LOGFLF(LogLevel::error, "mac capture start failed:",
             startErr ? startErr.localizedDescription.UTF8String : "?");
      // 同 addStreamOutput 失败路径: 失败即归还桥接对象, 不留到下次 open 覆盖
      {
        id obj = (__bridge_transfer id)streamOutput;
        obj = nil;
      }
      streamOutput = nullptr;
      outputQueue = nullptr;
      dispatch(&IVideoSourceOb::onVideoError, AVError::deviceError,
               "stream start failed");
      return false;
    }
    stream = (__bridge_retained void*)s;
    // 回报实际生效信息(会话落 session.json / 光标映射用)
    CaptureInfo capInfo = {};
    capInfo.cursorMode = cursorMode.c_str();
    if (bRegion) {
      capInfo.regionX = repX;
      capInfo.regionY = repY;
      capInfo.regionW = repW;
      capInfo.regionH = repH;
    }
    dispatch(&IVideoSourceOb::onCaptureInfo, capInfo);
    startTask();
    LOGFLF(LogLevel::info, "mac capture stream on, display:", (int32_t)displayId,
           " out:", outW, "x", outH, " fps:", fps, " cursor:", cursorMode);
    return true;
  }
  LOGFLF(LogLevel::error, "mac capture requires macOS 12.3+");
  dispatch(&IVideoSourceOb::onVideoError, AVError::deviceError,
           "ScreenCaptureKit unavailable");
  return false;
}

void MacCapture::onClose() {
  // 先停节拍线程(不再出帧), 再停 SCK(回调停), 最后清理
  stopTask();
  SCStream* s = (__bridge_transfer SCStream*)stream;
  stream = nullptr;
  if (s) {
    dispatch_semaphore_t sem = dispatch_semaphore_create(0);
    [s stopCaptureWithCompletionHandler:^(NSError*) {
      dispatch_semaphore_signal(sem);
    }];
    dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, 3LL * NSEC_PER_SEC));
    [s removeStreamOutput:(id<SCStreamOutput>)(__bridge id)streamOutput
                     type:SCStreamOutputTypeScreen
                    error:nil];
  }
  // 归还 arc 释放(__bridge_retained 的 +1 经 id 桥回)
  {
    id obj = (__bridge_transfer id)streamOutput;
    obj = nil;
  }
  streamOutput = nullptr;
  outputQueue = nullptr;  // queue 单实例泄漏量级可忽略(create 的 +1 归 void*)
  {
    std::lock_guard<std::mutex> lock(frameMtx);
    if (pendingBuffer) {
      CFRelease((CFTypeRef)pendingBuffer);
      pendingBuffer = nullptr;
    }
  }
  if (lastEmitted) {
    CFRelease((CFTypeRef)lastEmitted);
    lastEmitted = nullptr;
  }
  bContentInit = false;
}

void MacCapture::onRunTask() {
  // CFR 绝对到期节拍(与 Windows 严格路同构): pts=排期网格, tick 由时钟反推,
  // 慢帧跳拍记 missedTicks; 屏幕静止(SCK 不产帧)重发最后一帧不拷贝
  const double fps =
      (bCaptureParamSet && captureParam.fps > 0) ? captureParam.fps : 30.0;
  const int64_t fpsTick = getFrameTick(fps);
  int64_t t0 = timeTick();
  int64_t tick = 0;
  while (running()) {
    int64_t due = t0 + tick * fpsTick;
    while (running() && timeTick() < due) {
      sleepToTick(due);
    }
    if (!running()) {
      break;
    }
    while (timeTick() < due) {
    }
    int64_t framePts = (int64_t)((double)tick * 1000.0 / fps + 0.5);
    int64_t prevTick = tick;
    tick = (timeTick() - t0) / fpsTick + 1;
    if (tick > prevTick + 1) {
      missedTicks += tick - prevTick - 1;
    }
    void* cur = nullptr;
    {
      std::lock_guard<std::mutex> lock(frameMtx);
      if (pendingBuffer) {
        if (lastEmitted) {
          CFRelease((CFTypeRef)lastEmitted);
        }
        lastEmitted = pendingBuffer;
        pendingBuffer = nullptr;
      }
      cur = lastEmitted;
    }
    if (!cur) {
      continue;  // 首帧未到
    }
    CVPixelBufferRef pb = (CVPixelBufferRef)cur;
    int32_t w = (int32_t)CVPixelBufferGetWidth(pb);
    int32_t h = (int32_t)CVPixelBufferGetHeight(pb);
    if (!bContentInit) {
      bContentInit = true;
      contentW = w;
      contentH = h;
    } else if (w != contentW || h != contentH) {
      // v1 契约: 首帧后尺寸变化=显示设置变化, 停录(同 Windows)
      LOGFLF(LogLevel::error, "mac capture display changed:", contentW, "x",
             contentH, " ->", w, "x", h);
      dispatch(&IVideoSourceOb::onVideoError, AVError::dataNoVaild,
               "display changed during capture");
      break;
    }
    GpuFrame frame = {};
    frame.buffer = cur;
    frame.format.width = w;
    frame.format.height = h;
    frame.format.type = YuvType::nv12;
    frame.pts = framePts;
    frame.dts = framePts;
    onFrame(frame);
  }
}

// ============== 管理器 ==============

MacCaptureMgr::MacCaptureMgr() {
  sdkType = VDeviceSdk::mac_capture;
  onRefreshDevices();
}

void MacCaptureMgr::onRefreshDevices() {
  // 清空重建; 打开中/被会话占用的源 pin 住(与 CaptureWindowsMgr 同构)
  std::vector<DevicePtr> fresh;
  uint32_t ids[16] = {};
  uint32_t n = 0;
  CGGetOnlineDisplayList(16, ids, &n);
  for (uint32_t i = 0; i < n; i++) {
    if (CGDisplayIsAsleep(ids[i])) {
      continue;
    }
    DevicePtr dev = std::make_shared<MacCapture>();
    dev->setDisplay(ids[i], (int32_t)i);
    fresh.push_back(dev);
  }
  std::vector<DevicePtr> kept;
  for (auto& dev : devices) {
    if (!dev) {
      continue;
    }
    if (!dev->bOpening() && !isVideoSourceBusy(dev.get())) {
      continue;
    }
    fresh.erase(std::remove_if(fresh.begin(), fresh.end(),
                               [&](const DevicePtr& d) {
                                 return d->getDisplayId() ==
                                        dev->getDisplayId();
                               }),
                fresh.end());
    kept.push_back(dev);
  }
  fresh.insert(fresh.end(), kept.begin(), kept.end());
  devices.swap(fresh);
}

void regMacCaptureDevice() {
  RegFunc regFunc = {"mac screencapturekit capture device init", []() {
                       AvoxManager::Get().vDeviceMgr.regMgrObj(
                           VDeviceSdk::mac_capture, []() -> IVideoManager* {
                             return new MacCaptureMgr();
                           });
                     }};
  AvoxManager::Get().initFuncs.push_back(regFunc);
}

}
