// macOS playtest --win 窗口宿主: AppKit 窗口 + CAMetalLayer 顺序渲染各拉流用例画面,
// 左上角走查说明条 (当前用例 + 该看到什么, 人据此判画面) + 判定横幅滚动最近判定行
// (对齐 Windows --win 的 WalkBanner / Android PlayMatrixActivity / iOS avoxtest)。
// 判定行经 StdoutTee.onLine 喂入, 日志照旧全量镜像 pm_log.txt (大模型事后复判)。
// 关窗 = windowWillClose 置取消 → 矩阵当前用例跑完即停, 剩余不再开跑 (对齐 Windows)。
// 注意: 从 SSH 会话直跑且无 Aqua 会话时窗口贴不上屏, 矩阵仍离屏跑完, 退出码不受影响
#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>

#include <atomic>
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

// 界面走查取消旗标: 关窗置位, runAll 在下一条用例前退出 (判定行口径不变)
std::atomic<bool> g_walkCancel{false};

@interface PmWalkDelegate : NSWindowDelegate
@end
@implementation PmWalkDelegate
- (void)windowWillClose:(NSNotification*)note {
  (void)note;
  g_walkCancel = true;
}
@end

namespace {
NSTextView* g_banner = nil;
NSTextView* g_caseLabel = nil;  // 左上角走查说明: [i/N] id + desc
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

// 用例开始: 左上角说明条更新 (onCaseStart 从矩阵线程来, 丢主线程改 UI)
void bannerCase(const std::string& head, const std::string& desc) {
  NSString* h = [NSString stringWithUTF8String:head.c_str()];
  NSString* d = [NSString stringWithUTF8String:desc.c_str()];
  dispatch_async(dispatch_get_main_queue(), ^{
    if (!g_caseLabel) return;
    g_caseLabel.string = [NSString stringWithFormat:@"%@\n%@", h, d];
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
    // 关窗后矩阵还差当前用例收尾 (取消只在下条用例前生效), 别先把窗口/渲染层放了
    win.releasedWhenClosed = NO;
    PmMacView* view = [[PmMacView alloc] initWithFrame:win.contentView.bounds];
    // 显式建 CAMetalLayer 挂给 view: 不能读 view.layer 等它提升 —— 窗口显示前
    // 拿到的是 NSViewBackingLayer, setDevice: 直接 unrecognized selector 崩
    CAMetalLayer* layer = [CAMetalLayer layer];
    layer.frame = win.contentView.bounds;
    [view setLayer:layer];
    view.wantsLayer = YES;
    win.contentView = view;
    layer.device = MTLCreateSystemDefaultDevice();
    PmWalkDelegate* walkDel = [[PmWalkDelegate alloc] init];
    win.delegate = walkDel;
    CGFloat lh = 64, bh = 96;
    // 左上角走查说明条: 当前用例 + 该看到什么 (人据此判画面); 判定横幅在其下方
    NSTextView* label = [[NSTextView alloc]
        initWithFrame:NSMakeRect(8, view.bounds.size.height - lh - 8,
                                 view.bounds.size.width - 16, lh)];
    label.editable = NO;
    label.drawsBackground = YES;
    label.backgroundColor = [NSColor colorWithWhite:0 alpha:0.6];
    label.textColor = [NSColor whiteColor];
    label.font = [NSFont systemFontOfSize:13 weight:NSFontWeightSemibold];
    label.textContainerInset = NSMakeSize(6, 4);
    label.string = @"avoxtest 界面走查 — 等待用例… (关窗即停矩阵)";
    // 顶边钉住: 高度变化时弹性的是下边距 (minY), 宽度随窗口伸缩
    label.autoresizingMask = NSViewWidthSizable | NSViewMinYMargin;
    [win.contentView addSubview:label];
    g_caseLabel = label;
    // 判定横幅 (顶部半透明 monospace): 底下全幅 Metal 层正在出当前用例画面
    g_lines = [NSMutableArray array];
    NSTextView* banner = [[NSTextView alloc]
        initWithFrame:NSMakeRect(8, view.bounds.size.height - lh - bh - 12,
                                 view.bounds.size.width - 16, bh)];
    banner.editable = NO;
    banner.drawsBackground = YES;
    banner.backgroundColor = [NSColor colorWithWhite:0 alpha:0.6];
    banner.textColor = [NSColor whiteColor];
    banner.font = [NSFont monospacedSystemFontOfSize:11 weight:NSFontWeightRegular];
    banner.textContainerInset = NSMakeSize(6, 4);
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
    // 走查钩子: 值拷贝补 onCaseStart/cancel, 无头路径 (headless CLI) 不受影响
    RunOptions wopt = opt;
    wopt.cancel = &g_walkCancel;
    wopt.onCaseStart = [](const PlayCase& c, int32_t idx, int32_t total) {
      std::string head = "[" + std::to_string(idx + 1) + "/" + std::to_string(total) +
                         "] " + c.id + "  ● 运行中";
      bannerCase(head, c.desc.empty() ? c.url : c.desc);
    };
    __block int code = 1;
    dispatch_async(dispatch_get_global_queue(0, 0), ^{
      code = runAll(cases, (__bridge void*)layer, wopt);
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
