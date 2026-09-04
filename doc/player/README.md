# 播放器文档子集

avox 播放器 (IMediaPlayer/ISourcePlayer) 相关文档总索引。架构总览见 [Code Wiki](../code-wiki/README.md), 虚拟制片相关见 [virtualproduction](../virtualproduction/README.md)。

## 阅读顺序建议

1. [播放器项目](播放器项目.md) — 播放器整体设计
2. [播放器功能](core/播放器功能.md) → [播放器时间](core/播放器时间.md) — 功能与同步主线
3. 按需查阅各子目录

## 总览

| 文档 | 说明 |
|------|------|
| [播放器项目](播放器项目.md) | 播放器整体设计 |

## core/ — 核心设计与功能

| 文档 | 说明 |
|------|------|
| [数据源](core/数据源.md) | AVSource/RawSource 数据源设计 |
| [播放器功能](core/播放器功能.md) | 软硬解切换、Seek |
| [播放器时间](core/播放器时间.md) | 音视频同步、倍速播放、低延迟 |
| [播放器埋点](core/播放器埋点.md) | 埋点系统设计 |

## decode/ — 解码与渲染

| 文档 | 说明 |
|------|------|
| [播放器硬解](decode/播放器硬解.md) | 硬解框架与各平台接入 |
| [播放器FFmpeg](decode/播放器FFmpeg.md) | FFmpeg 软解/硬解集成 |
| [多平台GPU共享](decode/多平台GPU共享.md) | Vulkan/DX11 跨设备/跨 API 纹理共享 (godot/Unity 插件 GPU 直通基础) |
| [Vulkan滤境时间](decode/Vulkan滤境时间.md) | 滤镜性能数据 |

## media/ — 流媒体与码流

| 文档 | 说明 |
|------|------|
| [ZLMediaKit](media/ZLMediaKit.md) | 直播流协议解析 (avox_zlmediakit) |
| [RTSP点播Seek](media/RTSP点播Seek.md) | RTSP 点播 seek 实现 |
| [H264](media/H264.md) | H264 码流知识 |

## platform/ — 平台集成

| 文档 | 说明 |
|------|------|
| [播放器Android](platform/播放器Android.md) | Android 集成 (硬解见 [platforms/android](../platforms/android/Android硬解Vulkan.md)) |
| [播放器IOS](platform/播放器IOS.md) | iOS 集成 (硬解见 [platforms/ios](../platforms/ios/IOS硬解Vulkan.md)) |
| [播放器Vulkan集成](platform/播放器Vulkan集成.md) | Vulkan 跨平台集成 |
| [播放器Electron](platform/播放器Electron.md) | Electron 集成 (含 GPU 共享/网页渲染实证) |
| [Avalonia播放器](platform/Avalonia播放器.md) | C# Avalonia 跨平台 Demo |
| [播放器多平台Demo](platform/播放器多平台Demo.md) | 多平台演示 |

## dev/ — 演进与问题

| 文档 | 说明 |
|------|------|
| [播放器重构](dev/播放器重构.md) | 重构历程与成果 |
| [播放器完善](dev/播放器完善.md) | 现代化改造规划 |
| [播放器问题记录](dev/播放器问题记录.md) | 同步、硬解格式、拆合包等问题 |

## WebRTC 相关

见 [webrtc 子集](../webrtc/) — 集成、3A 音频、双向通话、编解码器对接。

## 多引擎插件 (帧输出/直通)

| 引擎 | 位置 | 帧通路 |
|------|------|--------|
| Godot | [platform/godot/plugin](../../platform/godot/plugin/README.md) | Vulkan 零拷贝直通 + CPU 回退 |
| UE | [platform/ue](../../platform/ue/README.md) | CPU 路径 (UpdateTextureRegions) |
| Unity | [platform/unity](../../platform/unity/README.md) | Vulkan 直通 + IssuePluginCustomTextureUpdateV2 回退 |
