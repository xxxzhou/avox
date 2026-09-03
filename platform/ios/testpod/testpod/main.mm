//
//  main.m
//  testpod
//
//  Created by 周鑫 on 2025/5/28.
//

#import <UIKit/UIKit.h>
#import "AppDelegate.h"

#include <avox/AvoxCore.h>
#include <iostream>

using namespace avox;

int main(int argc, char * argv[]) {
    NSString * appDelegateClassName;
    @autoreleasepool {
        // Setup code that might create autoreleased objects goes here.
        appDelegateClassName = NSStringFromClass([AppDelegate class]);
    }
    // 调用 timeStampMS() 函数获取时间戳
    int64_t timestamp = avox::timeTickMS();
    std::cout << timestamp << std::endl;
    // 创建me
    IMediaPlayer* mp = avox::createMediaPlayer();
    mp->open("rtsp://192.168.68.244/live/test");
    
    return UIApplicationMain(argc, argv, nil, appDelegateClassName);
}
