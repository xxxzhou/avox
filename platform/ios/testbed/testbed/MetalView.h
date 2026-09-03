//
//  MetalView.h
//  testbed
//
//  Created by 周鑫 on 2025/5/29.
//
#import <UIKit/UIKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

@interface MetalView : UIView
@property (nonatomic, strong) CAMetalLayer *metalLayer;
@end
