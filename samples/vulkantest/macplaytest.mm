#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include "avox/AvoxImage.h"
#include "avox/AvoxPlayer.h"

using namespace avox;

// macOS 有窗口播放样例: 自建 NSWindow + CAMetalLayer 上屏。
// Windows 下 setSurface(nullptr) 由 SDK 自建窗口, Apple 下没有这条路 —— 平台原生
// 窗口一律由宿主提供 (Window.hpp 里 AvoxSurfaceType 就是 CAMetalLayer*), 故样例自建。
// 键位: p 存一帧 PNG, q/关窗退出。第 4 个参数给存图路径时每 3 秒自动覆盖存一张,
// 远程无 VNC 也能靠轮询这张图看画面 (SSH 里按不了键)。

// 背衬层换成 CAMetalLayer, 供 MetalRender 直接上屏
@interface AvoxMetalView : NSView
@end
@implementation AvoxMetalView
- (CALayer*)makeBackingLayer {
  return [CAMetalLayer layer];
}
@end

class MacPlayOb : public IMediaPlayerOb {
 public:
  void onStateChange(PlayerState pre, PlayerState cur) override {
    fprintf(stderr, "[mac] state: %s -> %s\n", getPlayerStateStr(pre),
            getPlayerStateStr(cur));
  }
  void onIoError(AVError error, const char* msg) override {
    fprintf(stderr, "[mac] io error %d: %s\n", (int)error, msg ? msg : "(null)");
  }
  void onDecodeError(TrackType track, DecodeResult error) override {
    fprintf(stderr, "[mac] decode error track=%s: %s\n",
            getTrackTypeStr(track), getDecodeResultStr(error));
  }
};

// 抓当前渲染帧存 PNG (非 Vulkan 时 screenShot 走 pVideoRender, 见 SurfaceRenderNative)
static bool saveShot(ISurfaceRender* render, const char* path) {
  IImageBuffer* buf = createImageBuffer();
  bool ok = false;
  if (buf && render->screenShot(buf)) {
    const ImageFormat fmt = buf->getImageFormat();
    ok = saveImagePath(path, buf);
    fprintf(stderr, "[mac] screenshot %s %dx%d: %s\n", path, fmt.width,
            fmt.height, ok ? "ok" : "save failed");
  } else {
    fprintf(stderr, "[mac] screenshot failed: screenShot returned false\n");
  }
  delete buf;
  return ok;
}

int main(int argc, char* argv[]) {
  const char* url = argc > 1 ? argv[1] : "rtsp://127.0.0.1:554/live/avox264";
  // 0 = 播到关窗为止; >0 = 到点自动退出 (无人值守回归用)
  const int timeoutSec = argc > 2 ? atoi(argv[2]) : 0;
  const char* ioPlanArg = argc > 3 ? argv[3] : nullptr;
  // 给了路径就每 3 秒自动存一张; 没给也能按 p 手动存到这个默认路径
  const char* shotPath = argc > 4 ? argv[4] : nullptr;
  const char* keyShotPath = shotPath ? shotPath : "/tmp/avox_shot.png";
  fprintf(stderr, "[mac] url: %s, timeout: %ds, io: %s, shot: %s\n", url,
          timeoutSec, ioPlanArg ? ioPlanArg : "auto",
          shotPath ? shotPath : "off");
  @autoreleasepool {
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    const NSRect frame = NSMakeRect(0, 0, 1280, 720);
    NSWindow* window = [[NSWindow alloc]
        initWithContentRect:frame
                  styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                             NSWindowStyleMaskResizable)
                    backing:NSBackingStoreBuffered
                      defer:NO];
    window.title = @"avox macplaytest";
    AvoxMetalView* view = [[AvoxMetalView alloc] initWithFrame:frame];
    view.wantsLayer = YES;
    CAMetalLayer* layer = (CAMetalLayer*)view.layer;
    layer.device = MTLCreateSystemDefaultDevice();
    layer.contentsScale = window.backingScaleFactor;
    window.contentView = view;
    [window center];
    [window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
    IMediaPlayer* mp = createMediaPlayer();
    if (!mp) {
      fprintf(stderr, "[mac] createMediaPlayer failed\n");
      return 2;
    }
    MacPlayOb ob;
    addMediaPlayerOb(mp, &ob);
    ISurfaceRender* render = mp->getSurfaceRender();
    // MoltenVK 建实例常报 ERROR_INCOMPATIBLE_DRIVER, 直接走原生 Metal 渲染
    render->setVulkan(false);
    render->setSurface((__bridge void*)layer);
    if (ioPlanArg) {
      mp->setIoPlan(strcmp(ioPlanArg, "ffmpeg") == 0 ? IoPlan::ffmpeg
                                                     : IoPlan::zlmediakit);
    }
    mp->open(url);
    // 事件循环: 关窗/按 q/超时退出, 期间判定是否真的推进了播放
    const auto start = std::chrono::steady_clock::now();
    auto lastShot = start;
    bool played = false;
    bool shotOk = false;
    bool running = true;
    while (running && window.isVisible) {
      @autoreleasepool {
        NSEvent* event =
            [NSApp nextEventMatchingMask:NSEventMaskAny
                               untilDate:[NSDate dateWithTimeIntervalSinceNow:0.01]
                                  inMode:NSDefaultRunLoopMode
                                 dequeue:YES];
        if (event) {
          if (event.type == NSEventTypeKeyDown) {
            const NSString* keys = event.charactersIgnoringModifiers;
            const unichar key = keys.length > 0 ? [keys characterAtIndex:0] : 0;
            if (key == 'p') {
              shotOk = saveShot(render, keyShotPath) || shotOk;
            } else if (key == 'q') {
              running = false;
            }
          }
          [NSApp sendEvent:event];
        }
      }
      if (mp->getState() == PlayerState::playing && mp->getPosition() > 0) {
        played = true;
      }
      const auto now = std::chrono::steady_clock::now();
      // 起播后每 3 秒覆盖存一张, 供远程轮询看画面
      if (shotPath && played &&
          std::chrono::duration_cast<std::chrono::seconds>(now - lastShot)
                  .count() >= 3) {
        lastShot = now;
        shotOk = saveShot(render, shotPath) || shotOk;
      }
      const auto elapsed =
          std::chrono::duration_cast<std::chrono::seconds>(now - start).count();
      if (timeoutSec > 0 && elapsed >= timeoutSec) {
        running = false;
      }
    }
    printf("[AVOX][TEST] case=mac-window-play result=%s state=%s pos=%lldms "
           "fps=%.1f shot=%s\n",
           played ? "PASS" : "FAIL", getPlayerStateStr(mp->getState()),
           (long long)mp->getPosition(), mp->getFps(),
           shotPath ? (shotOk ? "ok" : "failed") : "off");
    removeMediaPlayerOb(mp, &ob);
    mp->close();
    delete mp;
    return played ? 0 : 1;
  }
}
