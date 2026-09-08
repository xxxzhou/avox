# 存入很大的编译的第三方库链接

## FFmpeg

**当前版本: FFmpeg 9.0.1,五平台统一自编**(不再使用第三方预编译包,旧来源仅存档:
[windows BtbN](https://github.com/BtbN/FFmpeg-Builds/releases) /
[windows gyan](https://www.gyan.dev/ffmpeg/builds/) /
[ios guanweidong(GPL,已弃用)](https://github.com/guanweidong/ffmpeg7.0) /
[mobile-ffmpeg](https://github.com/tanersener/mobile-ffmpeg/tree/master) /
[linux BtbN,已弃用](https://github.com/BtbN/FFmpeg-Builds/releases))

- 白名单精简构建,许可 **LGPL**(无 x264/x265,可进闭源商业渠道)
- 构建脚本: [script/ffmpeg/build_ffmpeg.py](../../script/ffmpeg/build_ffmpeg.py)(Windows,
  `--flavor gpl|lgpl`)/ `build_ffmpeg_android.sh` / `build_ffmpeg_apple.sh`(macOS+iOS,在 Mac 上执行) /
  `build_ffmpeg_linux.sh`(Linux x64,在 WSL2/Ubuntu 上执行)
- 各平台目录: `windows/`(MinGW dll + MSVC .lib + zlib1/libwinpthread 运行时 dll)、
  `android/`(arm64-v8a .so,SONAME 无版本号)、`darwin/`(macOS arm64 .a)、`ios/`(arm64 真机 .a)、
  `linux/`(x64 .so,带主版本号名,TLS 走系统 OpenSSL)
- Apple 平台走 VideoToolbox 硬编硬解(h264_videotoolbox/hevc_videotoolbox),TLS 走 SecureTransport;
  Linux TLS 走 OpenSSL(libssl-dev),编码器仅内置 aac(无 MFT/libx264),vaapi 留待 avox linux 落地时加

```
// 查看H264支持的解码器
ffmpeg -decoders | findstr h264
```

## WebRTC

不使用libwebrtc,使用源码版本webrtc,需要新版本里的H265功能。

需要指定WebRTC源码位置，从源码位置编译出webrtc库。头文件与库都直接根据webrtc源码目录来设定，不把想关的文件复制到当前项目中，因为webrtc源码有太多文件了。