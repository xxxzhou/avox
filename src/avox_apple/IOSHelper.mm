#include "IOSHelper.h"
// 引入 Foundation 框架头文件
#include "MetalWindow.hpp"
#include "avox/module/AvoxManager.hpp"
#include <Foundation/Foundation.h>
#include <TargetConditionals.h>
#include <iostream>
#import <AVFoundation/AVFoundation.h>
#if TARGET_OS_IPHONE
#import <UIKit/UIKit.h>
#else
#import <AppKit/AppKit.h>
#endif

// 定义一个简单的通知处理对象（因为 NotificationCenter 需要一个 Target）
// 不能有命名空间
@interface AppLifecycleObserver : NSObject
- (void)handleWillResignActive;
- (void)handleDidBecomeActive;
@end

@implementation AppLifecycleObserver
static AppLifecycleObserver* lifecycleObserver = nil;

- (void)handleWillResignActive {
    // 调用 C++ 命名空间内的单例
    avox::AvoxManager::Get().enterBack(true);
}

- (void)handleDidBecomeActive {
    avox::AvoxManager::Get().enterBack(false);
}
@end

namespace avox {

// 动态库加载时调用
__attribute__((constructor)) void onLibraryLoad() {
  // 执行初始化操作
  AvoxManager::Get().init();
  // 可以添加其他初始化代码
  // 在主线程注册通知
  dispatch_async(dispatch_get_main_queue(), ^{
      lifecycleObserver = [[AppLifecycleObserver alloc] init];
      // 生命周期通知名按系统区分(UIApplication*/NSApplication*)
#if TARGET_OS_IPHONE
      NSString* willResignActive = UIApplicationWillResignActiveNotification;
      NSString* didBecomeActive = UIApplicationDidBecomeActiveNotification;
#else
      NSString* willResignActive = NSApplicationWillResignActiveNotification;
      NSString* didBecomeActive = NSApplicationDidBecomeActiveNotification;
#endif
      // 1. 即将进入后台 (最关键：此时 GPU 权限还在，赶紧停！)
      [[NSNotificationCenter defaultCenter] addObserver:lifecycleObserver
                                               selector:@selector(handleWillResignActive)
                                                   name:willResignActive
                                                 object:nil];

      // 2. 已经回到前台
      [[NSNotificationCenter defaultCenter] addObserver:lifecycleObserver
                                               selector:@selector(handleDidBecomeActive)
                                                   name:didBecomeActive
                                                 object:nil];
  });
}

// 动态库卸载时调用
__attribute__((destructor)) void onLibraryUnload() {
  // 执行清理操作
  // 可以添加其他清理代码
}

void logApple(const char *time, const char *level, const char *msg) {
  // NSString *timeStr = time ? [NSString stringWithUTF8String:time] : @"";
  // NSString *levelStr = level ? [NSString stringWithUTF8String:level] : @"";
  // NSString *msgStr = msg ? [NSString stringWithUTF8String:msg] : @"";
  // NSLog(@"[%@] %@: %@", timeStr, levelStr, msgStr);
  std::cout << "[" << time << "] " << level << ": " << msg << std::endl;
}

const char* getBundlePath(NSString* bundleName, NSString* resourceName){
  NSString *avoxBundlePath = [[NSBundle mainBundle] pathForResource:bundleName
                                                            ofType:nil];
  if (!avoxBundlePath) {
    return nullptr;
  }
  // 创建 avox.bundle 的 NSBundle 实例
  NSBundle *avoxBundle = [NSBundle bundleWithPath:avoxBundlePath];
  if (!avoxBundle) {
    return nullptr;
  }
  // 从 avox.bundle 中查找资源
  NSString *bundlePath = [avoxBundle pathForResource:resourceName ofType:nil];
  if (bundlePath) {
    const char *fullPath = [bundlePath UTF8String];
    return fullPath;
  } else {
    return nullptr;
  }
}

const char *getShaderPath(const char *spvPath) {
  // spvpath里自带glsl
  return getBundlePath(@"avox.bundle", [NSString stringWithUTF8String:spvPath]); 
}

const char *getFontPath(const char *fontName){  
  return getBundlePath(@"avox.bundle/fonts", [NSString stringWithUTF8String:fontName]); 
}

const char *getImagePath(const char *imageName){
  return getBundlePath(@"avox.bundle/images", [NSString stringWithUTF8String:imageName]); 
}

const char *getModelPath(const char *modelName){
  return getBundlePath(@"avox.bundle/models", [NSString stringWithUTF8String:modelName]);
}

float getIosDeviceSystemVersion() {
#if TARGET_OS_IPHONE
  return [[UIDevice currentDevice].systemVersion floatValue];
#else
  NSOperatingSystemVersion v = [[NSProcessInfo processInfo] operatingSystemVersion];
  return v.majorVersion + v.minorVersion / 100.0f;
#endif
}

void setIosAudioRoute(bool bSpeaker) {
#if TARGET_OS_IPHONE
    AVAudioSession *session = [AVAudioSession sharedInstance];
    NSError *error = nil;
    // 必须用 PlayAndRecord 才能支持 WebRTC 的双向通信和扬声器切换
    AVAudioSessionCategoryOptions options = AVAudioSessionCategoryOptionAllowBluetooth |
                                            AVAudioSessionCategoryOptionAllowBluetoothA2DP;
        if (bSpeaker) {
        options |= AVAudioSessionCategoryOptionDefaultToSpeaker;
    }
    [session setCategory:AVAudioSessionCategoryPlayAndRecord
             withOptions:options
                   error:&error];
    // 2. 设置 Mode
    // VideoChat 会优化扬声器表现，VoiceChat 倾向于听筒
    NSString *mode = bSpeaker ? AVAudioSessionModeVideoChat : AVAudioSessionModeVoiceChat;
    [session setMode:mode error:&error];
    // 3. 激活
    [session setActive:YES error:&error];
    if (error) {
        // 调用你之前的 logApple 记录错误
        logApple("", "warn", [[NSString stringWithFormat:@"SetAudioRoute Error: %@", error.localizedDescription] UTF8String]);
    }
#else
    // macOS 无 AVAudioSession, 输出路由走 CoreAudio 设备选择, 待真机补
    (void)bSpeaker;
#endif
}

}
