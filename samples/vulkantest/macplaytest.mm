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
// 键位: p 存一帧 PNG 到 /tmp/avox_shot.png, q/关窗退出。

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

// 抓当前渲染帧存 PNG
static void saveShot(ISurfaceRender* render) {
  IImageBuffer* buf = createImageBuffer();
  if (buf && render->screenShot(buf)) {
    const char* path = "/tmp/avox_shot.png";
    fprintf(stderr, "[mac] screenshot %s: %s\n", path,
            saveImagePath(path, buf) ? "ok" : "failed");
  } else {
    fprintf(stderr, "[mac] screenshot failed\n");
  }
  delete buf;
}

int main(int argc, char* argv[]) {
  const char* url = argc > 1 ? argv[1] : "rtsp://127.0.0.1:554/live/avox264";
  // 0 = 播到关窗为止; >0 = 到点自动退出 (无人值守回归用)
  const int timeoutSec = argc > 2 ? atoi(argv[2]) : 0;
  const char* ioPlanArg = argc > 3 ? argv[3] : nullptr;
  fprintf(stderr, "[mac] url: %s, timeout: %ds, io: %s\n", url, timeoutSec,
          ioPlanArg ? ioPlanArg : "auto");
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
    bool played = false;
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
              saveShot(render);
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
      const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                               std::chrono::steady_clock::now() - start)
                               .count();
      if (timeoutSec > 0 && elapsed >= timeoutSec) {
        running = false;
      }
    }
    printf("[AVOX][TEST] case=mac-window-play result=%s state=%s pos=%lldms "
           "fps=%.1f\n",
           played ? "PASS" : "FAIL", getPlayerStateStr(mp->getState()),
           (long long)mp->getPosition(), mp->getFps());
    removeMediaPlayerOb(mp, &ob);
    mp->close();
    delete mp;
    return played ? 0 : 1;
  }
}
