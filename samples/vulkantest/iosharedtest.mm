// Apple IOSurface GPU 直通端到端测试 (vksharedtest 的 Apple 版)
// 用法:
//   iosharedtest                     — 0xAB 回归模式 (口径对齐 win vksharedtest)
//   iosharedtest <video.mp4> [outdir] — 视频模式: 真播彩条片源, 从 Metal 消费端
//     (panvox/Flutter 同款链路) 抓 3 帧存 PNG, 供远程查看画面
// 链路: decode → Vulkan 图(MoltenVK) → enableVkOutput → getVkOutputHandle
//       (IOSurface) → CVPixelBuffer → CVMetalTextureCache → Metal blit → 读回
// 附带: 轮询期 ioSurfaceId 必须稳定 (图重建才会换面); Metal 对 IOSurface 无
//       跨 API 隐式同步, 连续多帧读回全对即旁证无撕裂

#import <CoreVideo/CVPixelBuffer.h>
#import <CoreVideo/CVMetalTextureCache.h>
#import <Metal/Metal.h>

#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>

#include "avox/AvoxCore.h"
#include "avox/AvoxImage.h"
#include "avox/AvoxLayer.h"
#include "avox/AvoxPlayer.h"

using namespace avox;

// ── Metal 消费端公共件: IOSurface → MTLTexture → blit 到共享内存 ──
// 返回持 blit 结果的 MTLBuffer (调用方读完释放), 尺寸 w*h*4 (BGRA 字节序)
static id<MTLBuffer> metalGrab(IOSurfaceRef ioSurface, id<MTLDevice> device,
                               id<MTLCommandQueue> queue, int32_t* outW,
                               int32_t* outH) {
  CVPixelBufferRef pb = nullptr;
  CVReturn cvRet = CVPixelBufferCreateWithIOSurface(nullptr, ioSurface, nullptr,
                                                    &pb);
  if (cvRet != kCVReturnSuccess || !pb) {
    std::cout << "[B] FAIL: CVPixelBufferCreateWithIOSurface: " << cvRet
              << std::endl;
    return nil;
  }
  const int32_t w = (int32_t)CVPixelBufferGetWidth(pb);
  const int32_t h = (int32_t)CVPixelBufferGetHeight(pb);
  CVMetalTextureCacheRef texCache = nullptr;
  CVMetalTextureCacheCreate(kCFAllocatorDefault, nullptr, device, nullptr,
                            &texCache);
  CVMetalTextureRef texRef = nullptr;
  cvRet = CVMetalTextureCacheCreateTextureFromImage(
      kCFAllocatorDefault, texCache, pb, nullptr, MTLPixelFormatBGRA8Unorm, w,
      h, 0, &texRef);
  if (cvRet != kCVReturnSuccess || !texRef) {
    std::cout << "[B] FAIL: CVMetalTextureCacheCreateTextureFromImage: "
              << cvRet << std::endl;
    CFRelease(pb);
    return nil;
  }
  id<MTLTexture> srcTex = CVMetalTextureGetTexture(texRef);
  const NSUInteger bytesPerRow = w * 4;
  id<MTLBuffer> outBuf =
      [device newBufferWithLength:(w * h * 4)
                          options:MTLResourceStorageModeShared];
  id<MTLCommandBuffer> cmd = [queue commandBuffer];
  id<MTLBlitCommandEncoder> blit = [cmd blitCommandEncoder];
  [blit copyFromTexture:srcTex
            sourceSlice:0
            sourceLevel:0
           sourceOrigin:MTLOriginMake(0, 0, 0)
             sourceSize:MTLSizeMake(w, h, 1)
              toBuffer:outBuf
         destinationOffset:0
    destinationBytesPerRow:bytesPerRow
  destinationBytesPerImage:w * h * 4];
  [blit endEncoding];
  [cmd commit];
  [cmd waitUntilCompleted];
  CFRelease(texRef);
  CFRelease(texCache);
  CFRelease(pb);
  *outW = w;
  *outH = h;
  return outBuf;
}

// BGRA → RGBA 后存 PNG (saveImagePath 按 imageType 转 PNG)
static bool saveGrabPng(id<MTLBuffer> buf, int32_t w, int32_t h,
                        const char* path) {
  IImageBuffer* img = createImageBuffer();
  ImageFormat fmt = {};
  fmt.width = w;
  fmt.height = h;
  fmt.imageType = ImageType::rgba8;
  fmt.rowPitch = w * 4;
  img->setImageFormat(fmt);
  const uint8_t* src = (const uint8_t*)[buf contents];
  uint8_t* dst = (uint8_t*)img->getPointer();
  for (int32_t i = 0; i < w * h; i++) {
    dst[i * 4 + 0] = src[i * 4 + 2];
    dst[i * 4 + 1] = src[i * 4 + 1];
    dst[i * 4 + 2] = src[i * 4 + 0];
    dst[i * 4 + 3] = src[i * 4 + 3];
  }
  const bool ok = saveImagePath(path, img);
  delete img;
  return ok;
}

static float matchRatio0xAB(const uint8_t* data, int32_t size) {
  int32_t match = 0;
  for (int32_t i = 0; i < size; i++) {
    if (data[i] == 0xAB) match++;
  }
  return (float)match / size * 100.0f;
}

// ── 0xAB 回归模式 ──
static int probe0xAB() {
  std::cout << "=== Apple IOSurface E2E Test (0xAB) ===" << std::endl;
  IImageRender* irA = createImageRender();
  ISurfaceRender* srA = irA->getSurfaceRender();
  srA->setOffSurface(YuvType::other);

  const int32_t W = 256, H = 256;
  const int32_t IMG_SIZE = W * H * 4;
  IImageBuffer* bufA = createImageBuffer();
  ImageFormat fmtA = {};
  fmtA.width = W;
  fmtA.height = H;
  fmtA.imageType = ImageType::rgba8;
  fmtA.rowPitch = W * 4;
  bufA->setImageFormat(fmtA);
  memset(bufA->getPointer(), 0xAB, IMG_SIZE);
  irA->render(bufA);
  std::cout << "[A] rendered 0xAB image" << std::endl;
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  VkSharedHandle handle = {};
  bool bOk = false;
  for (int i = 0; i < 50; i++) {
    irA->render(bufA);
    if (enableVkOutput(srA, W, H) && getVkOutputHandle(srA, &handle) &&
        handle.ioSurface) {
      bOk = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  std::cout << "[A] enableVkOutput+getVkOutputHandle: " << (bOk ? "OK" : "FAIL")
            << " ioSurface=0x" << std::hex << (uintptr_t)handle.ioSurface
            << std::dec << " id=" << handle.ioSurfaceId << std::endl;
  if (!bOk || !handle.ioSurface) {
    std::cout << "FAIL: no IOSurface exported" << std::endl;
    delete bufA;
    delete irA;
    return 1;
  }

  // 持续喂帧, 验证 ioSurfaceId 稳定 (无图重建不应换面)
  const uint64_t firstId = handle.ioSurfaceId;
  for (int i = 0; i < 10; i++) {
    irA->render(bufA);
    std::this_thread::sleep_for(std::chrono::milliseconds(33));
    VkSharedHandle h2 = {};
    if (!getVkOutputHandle(srA, &h2)) {
      std::cout << "FAIL: getVkOutputHandle broke mid-stream" << std::endl;
      delete bufA;
      delete irA;
      return 1;
    }
    if (h2.ioSurfaceId != firstId) {
      std::cout << "FAIL: ioSurfaceId churned without rebuild (" << firstId
                << " -> " << h2.ioSurfaceId << ")" << std::endl;
      delete bufA;
      delete irA;
      return 1;
    }
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(300));

  id<MTLDevice> device = MTLCreateSystemDefaultDevice();
  id<MTLCommandQueue> queue = [device newCommandQueue];
  int32_t w = 0, h = 0;
  id<MTLBuffer> outBuf = metalGrab((IOSurfaceRef)handle.ioSurface, device,
                                   queue, &w, &h);
  if (!outBuf) {
    delete bufA;
    delete irA;
    return 1;
  }
  const uint8_t* data = (const uint8_t*)[outBuf contents];
  const float ratio = matchRatio0xAB(data, w * h * 4);
  std::cout << "[B] Metal readback " << w << "x" << h
            << " match0xAB=" << ratio << "%" << std::endl;
  if (ratio > 50.0f) {
    std::cout << "PASS: data from A reached Metal via IOSurface" << std::endl;
  } else {
    std::cout << "FAIL: Metal readback has no 0xAB pattern" << std::endl;
  }

  disableVkOutput(srA);
  delete bufA;
  delete irA;
  std::cout << "=== DONE ===" << std::endl;
  return ratio > 50.0f ? 0 : 1;
}

// ── 视频模式: 真播片源, Metal 消费端抓帧存 PNG ──
static int probeVideo(const char* video, const char* outdir) {
  std::cout << "=== Apple IOSurface E2E Test (video) ===" << std::endl;
  IMediaPlayer* mp = createMediaPlayer();
  if (!mp) {
    std::cout << "FAIL: createMediaPlayer" << std::endl;
    return 1;
  }
  ISurfaceRender* render = mp->getSurfaceRender();
  // 强制 Vulkan 合成图 (enableVkOutput 走 VkVideoRender; Apple 默认 Metal 直渲)
  render->setVulkan(true);
  render->setOffSurface(YuvType::other);
  mp->open(video);

  // 等起播
  bool played = false;
  for (int i = 0; i < 200; i++) {
    if (mp->getState() == PlayerState::playing && mp->getPosition() > 0) {
      played = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  std::cout << "[A] playing: " << (played ? "OK" : "FAIL")
            << " pos=" << mp->getPosition() << "ms" << std::endl;
  if (!played) {
    std::cout << "FAIL: player did not start" << std::endl;
    return 1;
  }

  // enableVkOutput + 轮询 IOSurface (图随首帧异步建)
  VkSharedHandle handle = {};
  bool bOk = false;
  for (int i = 0; i < 50; i++) {
    if (enableVkOutput(render, 16, 16) && getVkOutputHandle(render, &handle) &&
        handle.ioSurface) {
      bOk = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  std::cout << "[A] enableVkOutput+getVkOutputHandle: " << (bOk ? "OK" : "FAIL")
            << " ioSurface=0x" << std::hex << (uintptr_t)handle.ioSurface
            << std::dec << " id=" << handle.ioSurfaceId << std::endl;
  if (!bOk || !handle.ioSurface) {
    std::cout << "FAIL: no IOSurface exported" << std::endl;
    return 1;
  }

  // Metal 消费端抓 3 帧 (间隔取, 时间码应前进), 每帧重取句柄 (契约: 换面重取)
  id<MTLDevice> device = MTLCreateSystemDefaultDevice();
  id<MTLCommandQueue> queue = [device newCommandQueue];
  uint8_t lastSample = 0;
  bool allSaved = true;
  bool framesDiffer = false;
  for (int f = 0; f < 3; f++) {
    VkSharedHandle hnd = {};
    if (!getVkOutputHandle(render, &hnd) || !hnd.ioSurface) {
      std::cout << "FAIL: handle lost at frame " << f << std::endl;
      return 1;
    }
    int32_t w = 0, h = 0;
    id<MTLBuffer> buf = metalGrab((IOSurfaceRef)hnd.ioSurface, device, queue,
                                  &w, &h);
    if (!buf) {
      std::cout << "FAIL: metal grab at frame " << f << std::endl;
      return 1;
    }
    // 帧推进判定: 中心点采样应随时间码变化
    const uint8_t* p = (const uint8_t*)[buf contents];
    const uint8_t sample = p[(size_t)(h / 2 * w + w / 2) * 4];
    if (f > 0 && sample != lastSample) {
      framesDiffer = true;
    }
    lastSample = sample;
    char path[512];
    snprintf(path, sizeof(path), "%s/ioshared_frame%d.png", outdir, f);
    const bool saved = saveGrabPng(buf, w, h, path);
    allSaved = allSaved && saved;
    std::cout << "[B] frame" << f << " " << w << "x" << h
              << " grab+save: " << (saved ? path : "FAIL")
              << " center=0x" << std::hex << (int)sample << std::dec
              << " pos=" << mp->getPosition() << "ms" << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(900));
  }

  if (allSaved && framesDiffer) {
    std::cout << "PASS: 3 frames grabbed from Metal via IOSurface, frames "
                 "advance, PNGs at " << outdir << std::endl;
  } else if (allSaved) {
    std::cout << "WARN: frames saved but center sample static (check video)"
              << std::endl;
  } else {
    std::cout << "FAIL: PNG save failed" << std::endl;
  }
  return allSaved ? 0 : 1;
}

int main(int argc, char* argv[]) {
  if (argc > 1) {
    return probeVideo(argv[1], argc > 2 ? argv[2] : "/tmp");
  }
  return probe0xAB();
}
