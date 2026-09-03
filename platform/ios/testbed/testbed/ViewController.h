//
//  ViewController.h
//  testbed
//
//  Created by 周鑫 on 2025/5/20.
//

#import <UIKit/UIKit.h>
#import "MetalView.h"

@interface ViewController : UIViewController

@property (weak, nonatomic) IBOutlet MetalView *metalView;
@property (weak, nonatomic) IBOutlet UIButton *btnStart;
@property (weak, nonatomic) IBOutlet UIButton *btnStop;

@end

