// macOS playtest --win 窗口宿主: AppKit 窗口 + CAMetalLayer 顺序渲染各拉流用例画面,
// 顶部横幅滚动最近判定行 (对齐 Windows --win / Android PlayMatrixActivity / iOS avoxtest)。
// 判定行经 StdoutTee.onLine 喂入, 日志照旧全量镜像 pm_log.txt (大模型事后复判)。
// 注意: 从 SSH 会话直跑且无 Aqua 会话时窗口贴不上屏, 矩阵仍离屏跑完, 退出码不受影响
#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>

#include <string>
#include <vector>

#include "playmatrix/PlayMatrix.hpp"
#include "playmatrix/PlayTee.hpp"

// Metal 层宿主 view: IMediaPlayer 认 CAMetalLayer, 与 avoxtest 同一挂法
@interface PmMacView : NSView
@end
@implementation PmMacView
+ (Class)layerClass {
  return [CAMetalLayer class];
}
@end

namespace {
NSTextView* g_banner = nil;
NSMutableArray<NSString*>* g_lines = nil;

// 横幅只留最近 6 行 (与 Android APK/iOS avoxtest 同口径)
void bannerLine(const std::string& line) {
  NSString* text = [NSString stringWithUTF8String:line.c_str()];
  dispatch_async(dispatch_get_main_queue(), ^{
    if (!g_banner) return;
    NSString* joined = nil;
    @synchronized(g_lines) {
      [g_lines addObject:text];
      NSUInteger n = [g_lines count];
      if (n > 6) {
        [g_lines removeObjectsInRange:NSMakeRange(0, n - 6)];
      }
      joined = [g_lines componentsJoinedByString:@"\n"];
    }
    [g_banner setString:joined];
  });
}
}  // namespace

namespace avox {
namespace playmatrix {

int runAppleWindowHost(const std::vector<PlayCase>& cases, const RunOptions& opt,
                       StdoutTee* tee) {
  @autoreleasepool {
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    NSWindow* win = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(60, 60, 960, 540)
                    styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                      backing:NSBackingStoreBuffered
                        defer:NO];
    win.title = @"avoxtest play matrix";
    PmMacView* view = [[PmMacView alloc] initWithFrame:win.contentView.bounds];
    // 显式建 CAMetalLayer 挂给 view: 不能读 view.layer 等它提升 —— 窗口显示前
    // 拿到的是 NSViewBackingLayer, setDevice: 直接 unrecognized selector 崩
    CAMetalLayer* layer = [CAMetalLayer layer];
    layer.frame = win.contentView.bounds;
    [view setLayer:layer];
    view.wantsLayer = YES;
    win.contentView = view;
    layer.device = MTLCreateSystemDefaultDevice();
    // 判定横幅 (顶部半透明 monospace): 底下全幅 Metal 层正在出当前用例画面
    g_lines = [NSMutableArray array];
    CGFloat bh = 96;
    NSTextView* banner = [[NSTextView alloc]
        initWithFrame:NSMakeRect(8, view.bounds.size.height - bh - 8,
                                 view.bounds.size.width - 16, bh)];
    banner.editable = NO;
    banner.drawsBackground = YES;
    banner.backgroundColor = [NSColor colorWithWhite:0 alpha:0.6];
    banner.textColor = [NSColor whiteColor];
    banner.font = [NSFont monospacedSystemFontOfSize:11 weight:NSFontWeightRegular];
    banner.textContainerInset = NSMakeSize(6, 4);
    // 顶边钉住: 高度变化时弹性的是下边距 (minY), 宽度随窗口伸缩
    banner.autoresizingMask = NSViewWidthSizable | NSViewMinYMargin;
    [win.contentView addSubview:banner];
    g_banner = banner;
    [win center];
    [win makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
    if (tee) {
      // 只喂判定行 ([AVOX][TEST] 前缀), SDK 日志走 pm_log.txt 不刷屏
      tee->onLine = [](const std::string& line) {
        if (line.rfind("[AVOX][TEST]", 0) == 0) {
          bannerLine(line);
        }
      };
    }
    __block int code = 1;
    dispatch_async(dispatch_get_global_queue(0, 0), ^{
      code = runAll(cases, (__bridge void*)layer, opt);
      // 矩阵跑完停 runloop: stop: 要再等一个事件才生效, 补发自定义事件
      dispatch_async(dispatch_get_main_queue(), ^{
        [NSApp stop:nil];
        NSEvent* ev = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                          location:NSMakePoint(0, 0)
                                     modifierFlags:0
                                         timestamp:0
                                      windowNumber:0
                                           context:nil
                                           subtype:0
                                             data1:0
                                             data2:0];
        [NSApp postEvent:ev atStart:YES];
      });
    });
    [NSApp run];  // 主线程泵事件 (对齐 Windows 宿主的消息泵), 矩阵结束即返回
    if (tee) {
      tee->onLine = nullptr;
    }
    return code;
  }
}

}  // namespace playmatrix
}  // namespace avox
