//
//  SceneDelegate.h
//  testbed
//
//  Created by 周鑫 on 2025/5/20.
//

#import <UIKit/UIKit.h>
#import "ViewController.h"

@interface SceneDelegate : UIResponder <UIWindowSceneDelegate>

@property (strong, nonatomic) UIWindow * window;

@property (strong, nonatomic) ViewController *viewController;
// 添加渲染类型属性
// @property (nonatomic, assign) bool bMetalRender;

- (ViewController *)getViewController;
@end

