//
//  SceneDelegate.m
//  testbed
//
//  Created by 周鑫 on 2025/5/20.
//

#import "SceneDelegate.h"
#include "avox_freetype/FreetypeExport.h"
#include "avox_apple/IOSHelper.h"
#include "avox/AvoxCore.h"
#include "avox/AvoxPlayer.h"
#include "avox_zlmediakit/ZlmExport.h"
#include <thread>

using namespace avox;

@interface SceneDelegate () {
  ISourcePlayer *sp;
}
@end

@implementation SceneDelegate

- (void)testMediaPlayer:(CAMetalLayer *)metalLayer {
  bool bMetalRender = true;
  // 测试媒体播放器功能
  IMediaPlayer *mediaPlayer = avox::createMediaPlayer();
  // 设置播放器参数
  mediaPlayer->setHardDecode(true);
  mediaPlayer->setIoPlan(IoPlan::zlmediakit);
  // 使用传入的MetalLayer设置渲染表面
  if (metalLayer) {
    void *surface = (__bridge void *)(metalLayer);
    mediaPlayer->getSurfaceRender()->setVulkan(!bMetalRender);
    mediaPlayer->getSurfaceRender()->setSurface(surface);
    NSLog(@"Metal layer set for media player");
  }
  mediaPlayer->open("rtsp://192.168.68.244/live/test");
  // mediaPlayer->open("rtsp://192.168.68.123:554/live/4C37DE52CsssBF3_0");
  // mediaPlayer->open("rtsp://122.9.67.221:554/rtp/33072701992000000001_33090301991320000001");
  // mediaPlayer->open("E://Back/为美好的世界献上爆焰12.mp4");
}

- (void)testDevicePlayer:(CAMetalLayer *)metalLayer {
  bool bMetalRender = false;
  sp = avox::createDevicePlayer();
  IAudioManager *audioMgr = getAudioManager(ADeviceSdk::ios);
  IVideoManager *videoMgr = getVideoManager(VDeviceSdk::ios_avf);
  int32_t count = videoMgr->getDeviceCount();
  int32_t vIndex = 0;
  sp->setAudioSource(audioMgr->getDevice(0));
  sp->setVideoSource(videoMgr->getDevice(vIndex));
  // 使用传入的MetalLayer设置渲染表面
  if (metalLayer) {
    void *surface = (__bridge void *)(metalLayer);
    sp->getSurfaceRender()->setVulkan(!bMetalRender);
    sp->getSurfaceRender()->setSurface(surface);
    NSLog(@"Metal layer set for media player");
  }
  //  enableRenderFont(sp->getSurfaceRender(), "simhei.ttf", {64, 1});
  //  renderDrawText(sp->getSurfaceRender(), "Hello, Vulkan 字体渲染测试12345!");
  LutParamet lut = {};
  lut.lutIndex = 1;
  sp->getSurfaceRender()->enableLut(lut);
  sp->open();
}

-(void)testRtcPlayer:(CAMetalLayer *)metalLayer{
    IRtcPlayer* rp = avox::createWebRtcPlayer();
      rp->setRollType(RtcRollType::offer);
      const char* url =
          "http://192.168.68.244/index/api/webrtc?app=live&stream=test&type=play";
    ISdpAgentOb*  sdpOb = avox::createZlTestSdpAgent(rp, url);
      rp->setSdpAgentOb(sdpOb);
    if (metalLayer) {
      void *surface = (__bridge void *)(metalLayer);
      rp->getRemoteSurfaceRender()->setSurface(surface);
      NSLog(@"Metal layer set for media player");
    }
    rp->open();
}

- (void)startRecord {
    if(!sp){
        return;
    }
  sp->getMuxer()->setMuxerType(MuxerType::ffmpeg);
  sp->getMuxer()->setHardEncode(true);
  sp->getMuxer()->setVideoCodec(VCodecId::h265);
  sp->getMuxer()->open("rtsp://192.168.68.244/live/test");
}

- (void)stopRecord {
    if(!sp){
        return;
    }
  sp->getMuxer()->close();
}

- (void)scene:(UIScene *)scene
    willConnectToSession:(UISceneSession *)session
                 options:(UISceneConnectionOptions *)connectionOptions {
  if ([scene isKindOfClass:UIWindowScene.class]) {
    UIWindowScene *windowScene = (UIWindowScene *)scene;
    // 从 storyboard 获取 window
    UIStoryboard *mainStoryboard = [UIStoryboard storyboardWithName:@"Main"
                                                             bundle:nil];
    self.window = [[UIWindow alloc] initWithWindowScene:windowScene];
    self.window.rootViewController =
        [mainStoryboard instantiateInitialViewController];
    [self.window makeKeyAndVisible];

    CAMetalLayer *metalLayer = nullptr;
    if ([windowScene.delegate isKindOfClass:SceneDelegate.class]) {
      ViewController *viewController =
          (ViewController *)self.window.rootViewController;
      if (viewController.metalView) {
        NSLog(@"Got MetalView in main.mm");
        // 获取 metalLayer
        metalLayer = viewController.metalView.metalLayer;
        // 获取开始结束button并绑定startRecord/stopRecord事件上
        UIButton *btnStart = viewController.btnStart;
        UIButton *btnStop = viewController.btnStop;
        [btnStart addTarget:self
                      action:@selector(startRecord)
            forControlEvents:UIControlEventTouchUpInside];
        [btnStop addTarget:self
                      action:@selector(stopRecord)
            forControlEvents:UIControlEventTouchUpInside];
      }
    }
    //[self testMediaPlayer:metalLayer];
    [self testDevicePlayer:metalLayer];
      //[self testRtcPlayer:metalLayer];
  }
}

- (ViewController *)getViewController {
  return self.viewController;
}

- (void)sceneDidDisconnect:(UIScene *)scene {
  // Called as the scene is being released by the system.
  // This occurs shortly after the scene enters the background, or when its
  // session is discarded. Release any resources associated with this scene that
  // can be re-created the next time the scene connects. The scene may
  // re-connect later, as its session was not necessarily discarded (see
  // `application:didDiscardSceneSessions` instead).
}

- (void)sceneDidBecomeActive:(UIScene *)scene {
  // Called when the scene has moved from an inactive state to an active state.
  // Use this method to restart any tasks that were paused (or not yet started)
  // when the scene was inactive.
}

- (void)sceneWillResignActive:(UIScene *)scene {
  // Called when the scene will move from an active state to an inactive state.
  // This may occur due to temporary interruptions (ex. an incoming phone call).
}

- (void)sceneWillEnterForeground:(UIScene *)scene {
  // Called as the scene transitions from the background to the foreground.
  // Use this method to undo the changes made on entering the background.
}

- (void)sceneDidEnterBackground:(UIScene *)scene {
  // Called as the scene transitions from the foreground to the background.
  // Use this method to save data, release shared resources, and store enough
  // scene-specific state information to restore the scene back to its current
  // state.
}

@end
