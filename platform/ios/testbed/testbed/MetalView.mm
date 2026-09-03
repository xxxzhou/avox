//
//  MetalView.m
//  testbed
//
//  Created by 周鑫 on 2025/5/29.
//
#import "MetalView.h"

@implementation MetalView

+ (Class)layerClass {
    // 指定视图的图层类为 CAMetalLayer
    return [CAMetalLayer class];
}

- (instancetype)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        [self setupMetalLayer];
    }
    return self;
}

- (instancetype)initWithCoder:(NSCoder *)coder {
    self = [super initWithCoder:coder];
    if (self) {
        [self setupMetalLayer];
    }
    return self;
}

- (void)setupMetalLayer {
    // 获取并配置 CAMetalLayer
    self.metalLayer = (CAMetalLayer *)self.layer;
    // self.metalLayer.device = MTLCreateSystemDefaultDevice();
    self.metalLayer.pixelFormat = MTLPixelFormatRGBA8Unorm;
    // self.metalLayer.framebufferOnly = YES;
    self.metalLayer.drawableSize = self.bounds.size;
}

- (void)layoutSubviews {
    [super layoutSubviews];
    // 当视图布局改变时，更新图层的可绘制大小
    self.metalLayer.drawableSize = self.bounds.size;
}

@end
