# avox_apple

Apple 平台层 (iOS + macOS)，按 `TargetConditionals.h` 的 `TARGET_OS_IPHONE`/`TARGET_OS_OSX` 细分。

- VideoToolbox 硬解/编码、Metal 渲染、AudioUnit 输出：两系统同一套 API，代码共用
- iOS 独有：AVAudioSession 会话/路由、UIKit 生命周期通知、RemoteIO
- macOS 分支：AudioUnit 走 DefaultOutput、AppKit 生命周期通知；音频路由切换与设备默认采样率查询待 Mac 真机补

## 文档

[继续深挖 Android/iOS 的图形内存共](https://dev.rinc.xyz/posts/221111-android-ios-shared-memory/)
