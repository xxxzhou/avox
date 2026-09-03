//
//  main.m
//  testbed
//
//  Created by 周鑫 on 2025/5/20.
//

#import <UIKit/UIKit.h>
#import "AppDelegate.h"
#import "SceneDelegate.h"
#import "ViewController.h"
#import "MetalView.h"
#include <avox/AvoxCore.h>
#include <Metal/Metal.h>
#include <QuartzCore/CAMetalLayer.h>
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
    // std::cout << appDelegateClassName.cString << std::endl;
    
    printf("Current timestamp in milliseconds: %lld\n", timestamp);
    // 打印时间戳到控制台
    NSLog(@"Current timestamp in milliseconds: %lld", timestamp);
    return UIApplicationMain(argc, argv, nil, appDelegateClassName);
}
