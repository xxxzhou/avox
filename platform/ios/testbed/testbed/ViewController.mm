//
//  ViewController.m
//  testbed
//
//  Created by 周鑫 on 2025/5/20.
//

#import "ViewController.h"

@interface ViewController ()
@end

@implementation ViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    // 设置视图背景色
    self.view.backgroundColor = [UIColor redColor];
    // Do any additional setup after loading the view.
    if (self.metalView) {
        NSLog(@"MetalView 绑定成功，地址: %p", self.metalView);
    } else {
        NSLog(@"MetalView 绑定失败，值为空");
    }
    
}

@end
