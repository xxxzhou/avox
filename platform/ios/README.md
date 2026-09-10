# IOS

## avoxtest — 功能矩阵测试 (推荐入口)

同一份 avoxtest.mm 编出 iOS 真机 app 与 macOS 无头 CLI, 自动跑
rtsp/rtmp/hls/ts/webrtc 六用例矩阵。构建与运行见
[avoxtest/README.md](avoxtest/README.md)。

## testbed — 原生 Xcode 手动集成参考

手动把 cmake 产物链进 Xcode 工程的步骤与踩坑记录, 见 [testbed/README.md](testbed/README.md)。

## 历史

CocoaPods 集成 (avox.podspec / avoxsdk / testpod) 已移除:
podspec 指向的 install/include 布局已不存在, 且 SDK 以 cmake 静态库交付;
后续需要 pod 交付时按 avoxtest/CMakeLists.txt 的依赖清单重建即可。
