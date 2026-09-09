#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>

#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include "avox/AvoxImage.h"
#include "avox/AvoxPlayer.h"

using namespace avox;

// macOS 有窗口 WebRTC 拉流样例: 走内置 ZLM/WHEP 信令, 远端画面上屏 (CAMetalLayer)。
// 与无头版 rtcplayheadless 的区别只在挂了 getRemoteSurfaceRender()->setSurface,
// 用于肉眼/截图确认真的出图, 而不只是 onFirstVideoFrame 回调到了。
// 键位: p 存一帧 PNG, q/关窗退出; 第 3 个参数给存图路径则连上后每 3 秒覆盖存一张。

// 背衬层换成 CAMetalLayer, 供 MetalRender 直接上屏
@interface AvoxRtcView : NSView
@end
@implementation AvoxRtcView
- (CALayer*)makeBackingLayer {
  return [CAMetalLayer layer];
}
@end

class MacRtcOb : public IRtcEventOb {
 public:
  std::atomic<bool> connected{false};
  std::atomic<bool> firstFrame{false};

  void onConnectionState(RtcConnState state) override {
    fprintf(stderr, "[rtc] conn: %s\n", getRtcConnStateStr(state));
    if (state == RtcConnState::connected) connected = true;
  }
  void onFirstVideoFrame() override {
    fprintf(stderr, "[rtc] first video frame\n");
    firstFrame = true;
  }
};

// 抓当前渲染帧存 PNG (远端渲染器)
static bool saveShot(ISurfaceRender* render, const char* path) {
  IImageBuffer* buf = createImageBuffer();
  bool ok = false;
  if (buf && render->screenShot(buf)) {
    const ImageFormat fmt = buf->getImageFormat();
    ok = saveImagePath(path, buf);
    fprintf(stderr, "[rtc] screenshot %s %dx%d: %s\n", path, fmt.width,
            fmt.height, ok ? "ok" : "save failed");
  } else {
    fprintf(stderr, "[rtc] screenshot failed: screenShot returned false\n");
  }
  delete buf;
  return ok;
}

int main(int argc, char* argv[]) {
  const char* url =
      argc > 1 ? argv[1]
               : "http://127.0.0.1/index/api/webrtc?app=live&stream=avox264&type=play";
  // 0 = 播到关窗为止; >0 = 到点自动退出
  const int timeoutSec = argc > 2 ? atoi(argv[2]) : 0;
  const char* shotPath = argc > 3 ? argv[3] : nullptr;
  const char* keyShotPath = shotPath ? shotPath : "/tmp/avox_rtc_shot.png";
  fprintf(stderr, "[rtc] url: %s, timeout: %ds, shot: %s\n", url, timeoutSec,
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
    window.title = @"avox macrtcplaytest";
    AvoxRtcView* view = [[AvoxRtcView alloc] initWithFrame:frame];
    view.wantsLayer = YES;
    CAMetalLayer* layer = (CAMetalLayer*)view.layer;
    layer.device = MTLCreateSystemDefaultDevice();
    layer.contentsScale = window.backingScaleFactor;
    window.contentView = view;
    [window center];
    [window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
    IRtcPlayer* player = createWebRtcPlayer();
    if (!player) {
      fprintf(stderr, "[rtc] createWebRtcPlayer failed\n");
      return 2;
    }
    player->setRollType(RtcRollType::offer);
    player->setVideoDirection(RtpDirection::recvOnly);
    // 只验画面, 不协商音频, 免依赖音频设备
    player->setAudioDirection(RtpDirection::inactive);
    MacRtcOb ob;
    player->addOb(&ob);
    IRtcEventOb* sdpAgent = createZlTestSdpAgent(player, url);
    player->addOb(sdpAgent);
    // 远端画面上屏; MoltenVK 建实例常报 ERROR_INCOMPATIBLE_DRIVER, 走原生 Metal
    ISurfaceRender* render = player->getRemoteSurfaceRender();
    render->setVulkan(false);
    render->setSurface((__bridge void*)layer);
    player->open();
    const auto start = std::chrono::steady_clock::now();
    auto lastShot = start;
    bool shotOk = false;
    bool running = true;
    double fps = 0;
    float loss = 0;
    int32_t rtt = -1;
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
      if (ob.connected) {
        fps = player->getFps();
        loss = player->getLossRate();
        rtt = player->getRttMs();
      }
      const auto now = std::chrono::steady_clock::now();
      // 出图后每 3 秒覆盖存一张, 供远程轮询看画面
      if (shotPath && ob.firstFrame && fps > 0 &&
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
    const bool pass = ob.connected && ob.firstFrame && fps > 0;
    printf("[AVOX][TEST] case=mac-rtc-window result=%s conn=%d firstFrame=%d "
           "fps=%.1f loss=%.2f rtt=%d shot=%s\n",
           pass ? "PASS" : "FAIL", ob.connected.load(), ob.firstFrame.load(),
           fps, loss, rtt, shotPath ? (shotOk ? "ok" : "failed") : "off");
    player->removeOb(&ob);
    player->removeOb(sdpAgent);
    player->close();
    delete player;
    return pass ? 0 : 1;
  }
}
