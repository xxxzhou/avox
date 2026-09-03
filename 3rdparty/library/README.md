# 存入很大的编译的第三方库链接

## FFmpeg

[windows](https://github.com/BtbN/FFmpeg-Builds/releases)
[windows2](https://www.gyan.dev/ffmpeg/builds/)
[android](https://gitcode.com/open-source-toolkit/dd3c5/?utm_source=tools_gitcode&index=top&type=card&&isLogin=1)
[ios](https://github.com/guanweidong/ffmpeg7.0/tree/main/FFmpeg-iOS)
[ios 7.0 ffmpeg](https://sourceforge.net/projects/avbuild/)

[mobile-ffmpeg](https://github.com/tanersener/mobile-ffmpeg/tree/master)

```
// 查看H264支持的解码器
ffmpeg -decoders | findstr h264
```

## WebRTC

不使用libwebrtc,使用源码版本webrtc,需要新版本里的H265功能。

需要指定WebRTC源码位置，从源码位置编译出webrtc库。头文件与库都直接根据webrtc源码目录来设定，不把想关的文件复制到当前项目中，因为webrtc源码有太多文件了。