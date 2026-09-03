# 原生Xcode编译

## 注意事项

添加lib在build phases里的link binary with libraries里.
指明lib path在build settings里的Search Paths下的Library Search Paths.

debug下,testbed下的build phases下的link binary with libraries直接添加/build/ios/avplay/install/aarch64/Debug下的静态库进去,添加别的地方容易出现一些奇怪的问题,比如ffmpeg加载3rdparty/library/ios/ffmpeg/lib下的,视频的编解码加载不上,使用ffmpeg的重采样功能也有机率crash.
