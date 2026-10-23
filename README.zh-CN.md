# avox

**[English](README.md) | 简体中文**

avox 是一套 C++17 的跨平台音视频库：采集、硬解、Vulkan GPU 处理、渲染、硬编与推流走同一条管线。

播放器是其中最完整的模块。WebRTC 通话、AI（语音识别、翻译、图像修复、超分）和 XR / 虚拟制片模块按插件接在同一套接口上，产品可以只带用得到的那部分。

> C++17 · Windows / Android / iOS / macOS / Linux · AGPLv3，另有商业授权 · v1.0.0969

[![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Android%20%7C%20iOS%20%7C%20Linux-blue)]()
[![Language](https://img.shields.io/badge/Language-C%2B%2B%20%7C%20C%23%20%7C%20Java%20%7C%20JS%20%7C%20Python-orange)]()
[![License](https://img.shields.io/badge/License-AGPL_v3-important)](LICENSE)
[![Commercial License](https://img.shields.io/badge/Commercial-Available-success)]()
[![Version](https://img.shields.io/badge/Version-1.0.0969-green)]()
[![GitHub stars](https://img.shields.io/github/stars/xxxzhou/avox?style=social)](https://github.com/xxxzhou/avox)

avox 来自作者多年的音视频与 GPU 工作，在大模型辅助下整理成库。技术脉络从早期的 [OEIP](https://zhuanlan.zhihu.com/p/104027165)（UE4/Unity3D 多媒体管线）到 [aoce](https://github.com/xxxzhou/aoce)（Vulkan 跨平台 GPU 图像处理）；关键实现都写成了文章，见[技术实现解析](#技术实现解析)。

**目录** · [用 avox 构建的产品](#用-avox-构建的产品) · [能力一览](#能力一览) · [核心特性](#核心特性) · [技术实现解析](#技术实现解析) · [技术演进](#技术演进) · [快速开始](#快速开始) · [常见问题](#常见问题)

## 用 avox 构建的产品

**泛音 Panvox** 是基于 avox 的跨平台视频播放器：产品层用 Flutter，引擎层就是本库，通过 C ABI 复用播放、硬解、GPU 处理、字幕渲染与 AI 能力，覆盖 Windows / macOS / iOS / Android。

- **Windows** - [Microsoft Store](https://apps.microsoft.com/detail/9PG77H33P7K8)
- **iOS / iPadOS / macOS** - [App Store](https://apps.apple.com/cn/app/panvox-player/id6819539073)，同一个页面；macOS 需要 12.0 以上和 Apple M 系芯片
- **Android** - [内部分享包](https://play.google.com/apps/test/RQo-NZmDUok/ahAO29uNRS3gG4ZDC35BtgMLCLvoQyam1Kvd6wGKPphYU5aCzPQ-UiYxxlxAB4ZtodrH0fTBAETk3--ARIvcjm3KQT)（连点「Play 商店版本」7 次 → 开发者选项 → 内部应用分享）

产品里已经落地的功能：海报墙媒体库（直连 NAS / SMB / WebDAV / Jellyfin）、直连不转码、ASS/PGS/SRT 完整渲染、逐帧 HDR 色调映射、AI 字幕与识别打标、IPTV（m3u + EPG）。

<table align="center">
  <tr>
    <td valign="middle"><img src="assets/images/panvox/win_wall.png" width="590" alt="Windows - 海报墙媒体库"></td>
    <td valign="middle"><img src="assets/images/panvox/android_iptv.png" width="160" alt="Android - IPTV 频道"></td>
  </tr>
  <tr>
    <td valign="middle"><img src="assets/images/panvox/mac_wall.png" width="590" alt="macOS - 海报墙媒体库"></td>
    <td valign="middle"><img src="assets/images/panvox/ios_library.png" width="160" alt="iOS - 海报墙"></td>
  </tr>
</table>

产品介绍与实现解析：[panvox.net 产品页](https://panvox.net/panvox/)；文章：[《从开源引擎 avox 到成品：全平台播放器「泛音」》](https://panvox.net/panvox/articles/engine-to-product/)。

## 能力一览

- **一套代码，五个平台** - Windows / Android / iOS / macOS / Linux 共用一份 C++17 代码和一套 CMake 构建，硬解硬编、GPU 互操作、窗口系统的差异都收在平台层，扩展新平台只需实现平台层接口。
- **硬解帧不落 CPU** - DX11VA、MediaCodec、VideoToolbox 输出的原生 GPU 纹理直接进 Vulkan 管线，处理后回渲染或硬编，全程不经 CPU 内存中转。
- **接口写一次，四种语言可用** - 导出层是纯虚接口 + `create*` 工厂 + `addXxxOb/removeXxxOb` 回调，ABI 上不用 STL 类型；SWIG 从同一套头文件生成 C# / Java / Node.js / Python 绑定，回调类经 director 在各语言里直接继承覆写，新增接口不用逐语言补胶水。
- **WebRTC 不 fork** - 用封装模块补解码器、AAC、3A 音频和数据源/编码器映射，跟随上游版本升级，没有源码包袱。
- **AI 与 XR 按需加载** - 语音识别、翻译、图像修复、目标检测和虚拟制片模块都以动态插件或独立模块形态存在，播放主链路不带它们也能跑。

## 核心特性

### 播放

- **直播流** - RTSP/RTMP/HTTP-FLV/HLS 等协议、磁力链接、WebRTC；ZLMediaKit 与 FFmpeg 两条解析通路。
- **本地媒体** - 常见音视频格式。
- **设备采集** - 相机、麦克风、屏幕捕获共用一个数据源模型（Windows Media Foundation / Android NdkCamera2 OES 纹理直出 / iOS 相机）。
- **播放控制** - 音视频同步（音频/视频/外部时钟，仿 ffplay 逻辑）、倍速、直播低延迟自适应缓冲、软硬解动态切换、动态窗口切换、单播放器多路流。
- **跨平台渲染** - ISurfaceRender 统一 Vulkan / DX11 / DX12 / Metal / OpenGL ES 窗口渲染，各平台硬解帧零拷贝进对应后端（外部内存共享 / 纹理直通）。
- **取流与解码可插拔** - 取流、解码、编码分层解耦，各层独立成源、可单独替换（IAVSource / IRawSource 抽象），不绑定 FFmpeg。内置 FFmpeg（通用格式）、ZLMediaKit（RTSP/RTMP 直播）、WebRTC（实时通话）三条来路，按源切换（IoPlan）；私有协议实现 IAVSource 即注册为新的 IO 方案；三条来路共用同一批 H264/H265/AAC 软硬解码器与渲染管线。

### 跨平台硬解与零拷贝通路

| 平台 | 硬件解码 | 硬解帧 → Vulkan 零拷贝通路 | 渲染后端 |
|------|----------|---------------------------|----------|
| Windows | DX11 (D3D11VA) | DX11 纹理 ↔ Vulkan 互操作 | DX11/DX12/Vulkan |
| Android | MediaCodec | AHardwareBuffer ↔ Vulkan | OpenGL ES/Vulkan |
| iOS | VideoToolbox | IOSurface ↔ Vulkan | Metal/Vulkan |
| Linux | VAAPI（计划中） | - | Vulkan |

- 硬解输出的原生 GPU 纹理直接进入 Vulkan 处理管线，或映射到渲染窗口，不经 CPU 内存中转。
- Vulkan ↔ OpenGL ES（Android）、Vulkan ↔ DX11（Windows）纹理互通。
- Android 的跨 EGLContext 解码与渲染、iOS 经 Metal/IOSurface 的衔接，都有走通的实现。

### 硬编、推流与录制

- **Android** - MediaCodec GPU 编码：相机 OES 纹理经 Vulkan 处理后直接送编码器，不落地 CPU。
- **iOS** - VideoToolbox 硬编：相机直出帧或 Vulkan 处理结果直接编码。
- **FFmpeg 软编** - 全平台兜底；封装器支持写本地文件与 RTMP/RTSP 推流。
- **多平台双向视频通话** - 拉流、解码、渲染、编码、推流全链路（Windows/Android/iOS/Linux），各环节软硬方案可切换，支持同步录制到本地。

### Vulkan GPU 图像处理

- 100+ 滤镜与特效：GPUImage 全系滤镜移植为 Vulkan Compute Shader。
- Layer + PipeGraph 可组合的计算管线图，支持跨平台 GPU 数据映射（OpenGL ES / Metal / DX11 ↔ Vulkan）。
- FreeType + Compute Shader 文本渲染，用于时间戳与字幕叠加。
- Anime4K 超分整合（GLSL 转 Vulkan Compute Shader，适配内置管线组合）。
- 性能参考：1080p 单处理层 0.1~0.3ms（RTX 2070，见[滤镜耗时实测](https://zhuanlan.zhihu.com/p/1931653908276704034)）。

### WebRTC 整合

不改动 WebRTC 源码，以封装模块扩展：

- 播放器的 H264/H265 软硬解接入 WebRTC 解码工厂（含 WebRTC M138 的原生 H265 支持）。
- 集成 AAC 音频解码器（WebRTC 自身只支持 opus/g711）。
- 播放器数据源（IVideoSource / IAudioSource）与跨平台硬编编码器映射进 WebRTC 推流链路。
- WebRTC 3A 音频处理（AEC 回声消除 / ANS 降噪 / AGC 增益）接入采集与推流。
- WebRTC 帧直接渲染到播放器各平台的原生窗口。

### AI 能力

AI 模块以动态插件（`plugins/`）形式加载，运行期探测能力：

- **实时语音字幕** - SourcePlayer 采集麦克风或屏幕音频流式识别，约 200ms 延迟；MediaPlayer 播放视频时先识别、再翻译成字幕（Sherpa-ONNX + 神经机器翻译）。
- **AI 图像修复** - YOLO26-Seg 做水印/物体实例分割后修复，全自动（从帧差分 + 边缘分析的传统方案演进而来）。
- **AI 推理** - ONNX Runtime / NCNN 通用推理（YOLO 检测、人脸关键点等），支持 Vulkan 显存直连输入输出。
- **多模态 Agent** - LLM 集成，支持图文对话与工具链调用。

<p align="center">
  <img src="assets/images/panvox/win_ai_subtitle.png" width="720" alt="Windows - AI 字幕生成（泛音 Panvox）">
  <br><em>泛音 Panvox 中的 AI 字幕：本机识别、翻译后嵌入播放</em>
</p>

### XR / 虚拟制片 / 引擎直通

VR/MR 相机跟踪、虚拟制片标定和 MR 虚实融合都在生产环境验证过，已集成进 avox：

- **VR/MR 相机跟踪** - LED 虚拟拍摄相机跟踪，对标 Redspy/MoSys：单目 + IMU、红外反光点方案（[跟踪算法](https://zhuanlan.zhihu.com/p/705932365)）。
- **相机标定** - g2o 图优化内参标定、手眼标定、变焦镜头单图内参拟合（[实现解析](https://zhuanlan.zhihu.com/p/705988110)）。
- **MR 虚实融合** - XR 拍摄里的虚实相机混合，标定 + 畸变校正，重投影误差约 3 像素（[实现解析](https://zhuanlan.zhihu.com/p/705965901)）。
- **LED 幕墙点云** - 仿 SLAM 流程生成点云与 Mesh，用 ORB_SLAM3（[实现解析](https://zhuanlan.zhihu.com/p/705975323)）。
- **超低延迟传输** - Rivermax（GPU Direct RDMA）与 NDI 做局域网图像传输（[实现解析](https://zhuanlan.zhihu.com/p/706037895)，为避开 NDI 100ms 以上的延迟）。
- **游戏引擎直通** - UE5、Unity3D、Godot 纹理级零拷贝双向直通：相机或 GPU 管线 → 引擎纹理，引擎 RenderTarget → 管线 → 推流（`platform/ue`、`platform/unity`、`platform/godot`）。播放、通话与 AI 在引擎内原生可用，不是嵌一个播放窗口。
- **虚拟人（数字人）** - 驱动与渲染管线对接，模块正在集成。

<p align="center">
  <img src="assets/images/godot/avox_android_menu.png" width="360" alt="Android + Godot - GPU 直通播放磁力链接">
  <br><em>Android + Godot：纹理级直通播放磁力链接</em>
</p>

### 多语言 SDK

- **C++** 原生接口；**SWIG** 生成 C# / Java / Node.js / Python 绑定（含跨线程回调方案）。
- 各平台 UI Demo：Win32、Android（Java）、iOS（ObjC）、[Electron（JS）](https://zhuanlan.zhihu.com/p/1943737922852460208)、[Avalonia（C#）](https://zhuanlan.zhihu.com/p/1991912977230733329)。

## 技术实现解析

关键实现都写成了文章。**最新的重写版连载在 [panvox.net 技术系列《从零做一个全平台播放器》](https://panvox.net/tech/build/)**；更早的按主题文章在[天天不在的文章列表](https://www.zhihu.com/people/zhou-xin-12-70-21/posts)：

**泛音 Panvox 产品与实现（2026）**

- [从开源引擎 avox 到成品：全平台播放器「泛音」](https://panvox.net/panvox/articles/engine-to-product/) - 产品全貌：播放、片库、字幕、AI、IPTV
- [HDR 不同平台的播放实现：直通、整形与逐帧色调映射](https://panvox.net/panvox/articles/hdr/)
- [Flutter 与 avox 的视频帧交互：从 Texture 到原生窗口](https://panvox.net/panvox/articles/flutter-frames/)
- [ASS/PGS/SRT 的不同平台显示：三条渲染路与四块画布](https://panvox.net/panvox/articles/subtitles/)
- [avox 本体与插件的不同平台编译：动态还是静态，平台说了算](https://panvox.net/panvox/articles/build-plugins/)
- [4 倍速播放的实现：解码上限与 I 帧模式](https://panvox.net/panvox/articles/speed-4x/)
- [远程访问的实现：直连、中继与一次授权](https://panvox.net/panvox/articles/remote-access/) - 出门在外取家里片源

**播放器框架与多平台移植**

- [播放器框架](https://zhuanlan.zhihu.com/p/1924535781558555955) - 多平台播放器框架总体设计
- [播放器 FFmpeg](https://zhuanlan.zhihu.com/p/1924537408311001536) / [播放器 Android](https://zhuanlan.zhihu.com/p/1924537652897625121) / [播放器 iOS](https://zhuanlan.zhihu.com/p/1924537772141679194) / [多平台 Vulkan 集成](https://zhuanlan.zhihu.com/p/1925515227908268800) / [多平台 Demo](https://zhuanlan.zhihu.com/p/1925515494309492633)
- [ZLMediaKit 播放流程中的流媒体解析](https://zhuanlan.zhihu.com/p/1925912219486521028) - 直播协议解析
- [播放器时间相关功能](https://zhuanlan.zhihu.com/p/1948834769115780459) - 音视频同步、倍速、低延迟自适应

**零拷贝硬解 GPU 通路**

- [Vulkan 与 DX11 交互](https://zhuanlan.zhihu.com/p/349534525) - Windows 硬解纹理直通 Vulkan
- [Android 硬解经 AHardwareBuffer 到 Vulkan 管线](https://zhuanlan.zhihu.com/p/1933240978434679422)
- [iOS 硬解经 IOSurface 到 Vulkan 管线](https://zhuanlan.zhihu.com/p/1933834616818636429)
- [Android 下 Vulkan 与 OpenGL ES 纹理互通](https://zhuanlan.zhihu.com/p/302285687)
- [NdkCamera2 使用 OES 纹理渲染](https://zhuanlan.zhihu.com/p/1967658409848440079) - 相机零拷贝采集

**硬编与实时通话**

- [Android MediaCodec GPU 编码实践](https://zhuanlan.zhihu.com/p/1977347530166666089) / [iOS 硬编实践](https://zhuanlan.zhihu.com/p/1987119873403417887)
- [多平台双向视频通话](https://zhuanlan.zhihu.com/p/1987128340973368623) - 全链路软硬方案切换
- [播放器整合 WebRTC 音频 3A 处理](https://zhuanlan.zhihu.com/p/1963601511633389459)

**WebRTC 整合**

- [播放器 WebRTC](https://zhuanlan.zhihu.com/p/1929598509927076242) - 集成总览
- [WebRTC 集成本地播放器解码器实践](https://zhuanlan.zhihu.com/p/1938686818565494734) - H264/H265 软硬解接入
- [WebRTC 集成 AAC 解码器](https://zhuanlan.zhihu.com/p/1941208736648636312)
- [WebRTC 音视频源和编码器映射](https://zhuanlan.zhihu.com/p/2000598286206248428)

**Vulkan 图像处理**

- GPUImage 移植系列：（一）[高斯模糊与自适应阈值](https://zhuanlan.zhihu.com/p/356355306)、（二）[Harris 角点与导向滤波](https://zhuanlan.zhihu.com/p/359787506)、（三）[A 到 C 滤镜](https://zhuanlan.zhihu.com/p/364888786)、（四）[D 到 O 滤镜](https://zhuanlan.zhihu.com/p/369930003)、（五）[P 到 Z 滤镜](https://zhuanlan.zhihu.com/p/372843997)、[总结](https://zhuanlan.zhihu.com/p/373137758)、[安卓 Demo](https://zhuanlan.zhihu.com/p/388055520)
- [Vulkan 滤镜耗时](https://zhuanlan.zhihu.com/p/1931653908276704034) - 管线层级性能实测
- [播放器 Vulkan 渲染文本](https://zhuanlan.zhihu.com/p/1963601925418234137) - FreeType 字幕与时间戳
- [播放器整合 Anime4K 超分](https://zhuanlan.zhihu.com/p/2043258134546974507)

**AI 与算法**

- [播放器实时语音字幕识别与翻译](https://zhuanlan.zhihu.com/p/2035763455190373290)
- [AI 图像修复](https://zhuanlan.zhihu.com/p/2037495059705221313) - YOLO-Seg 水印去除
- [NCNN 优化实时面部关键点检测](https://zhuanlan.zhihu.com/p/407299327) / [使用 NCNN 的 Vulkan 输入输出](https://zhuanlan.zhihu.com/p/411106967) - 端侧推理显存直连
- [CUDA 版 GrabCut 的实现](https://zhuanlan.zhihu.com/p/59283449) - 早期 GPU 分割实践

**工程实践**

- [使用 SWIG 转换 C++ 到别的编程语言](https://zhuanlan.zhihu.com/p/379049985) / [SWIG 与 Node.js 原生扩展的跨线程回调](https://zhuanlan.zhihu.com/p/1969758141236372869)

## 技术演进

| 阶段 | 项目 | 主要产出 |
|------|------|----------|
| 2019-2020 | oeip（Windows） | CUDA/DX11 图像管线、FFmpeg 推拉流、UE4/Unity3D 纹理直通、CUDA GrabCut/导向滤波、YOLO 整合进游戏引擎 |
| 2020-2021 | [aoce](https://github.com/xxxzhou/aoce)（跨平台） | Vulkan Compute 图像管线、GPUImage 100+ 滤镜移植、Android/iOS 相机采集、蓝绿幕抠像、NCNN 端侧推理、SWIG 多语言 |
| 2024-至今 | avox | 播放器 SDK：多平台硬解硬编、零拷贝 GPU 通路、WebRTC 全套、AI 字幕/修复/超分、多语言 SDK；XR 与虚拟制片模块集成 |
| 2026 | **泛音 Panvox** | 基于 avox 的跨平台播放器：Flutter 产品层 + avox C ABI；Windows 与 Apple 双端已上架；播放、片库、字幕、AI、IPTV 全链路落地 |

XR / 虚拟制片的能力从 aoce 时期开始积累，当时已在真实拍摄中完整验证，现在整体在 avox 里。

## 快速开始

### 编译环境

- CMake 3.16+
- Visual Studio 2019+（Windows）
- NDK 26.1.10909125（Android）
- Xcode 14+（iOS）
- GCC/Clang 与 Vulkan SDK（Linux）

### 外部依赖库

大型依赖（WebRTC、ONNX Runtime、OpenCV 等）放在与本项目同级的 `../avox_library` 目录，默认自动查找；也可以用环境变量或 CMake 参数指定：

```bash
export AVOX_EXTERNAL_LIBRARY_DIR=/path/to/avox_library
# 或
cmake -DAVOX_EXTERNAL_LIBRARY_DIR=/path/to/avox_library ...
```

### 编译命令

```bash
python build_windows.py   # Windows x64
python build_android.py   # Android arm64-v8a/armeabi-v7a
python build_ios.py       # iOS arm64/x86_64
python build_mac.py       # macOS arm64/x64/universal（需 macOS + Xcode）
python build_linux.py     # Linux x64

# 单元测试（随构建自动编译，手动运行）
ctest --test-dir build/windows/avox --output-on-failure -C Release
```

编译脚本支持自定义构建类型（Debug/Release）和目标架构。**默认 Release，产物不带调试符号**；要调试请显式设 `AVOX_BUILD_TYPE=Debug`。细节见 [doc/build/构建.md](doc/build/构建.md) 和各脚本里的配置项。

## 目录结构

```
avox/
├── src/                       # 核心 SDK 源码
│   ├── avox/                   # 核心播放器框架（avox 模块）
│   │   ├── player/            # MediaPlayer / SourcePlayer
│   │   ├── source/            # 数据源（IAVSource / IRawSource）
│   │   ├── video/ audio/      # 视频/音频解码器基类
│   │   ├── codec/             # 编解码抽象
│   │   ├── muxer/             # 媒体封装
│   │   ├── subtitle/          # 字幕
│   │   ├── layer/             # 图层合成
│   │   ├── module/            # 动态模块管理（IModule / ModuleMgr）
│   │   └── neural/            # 神经网络与 AI 接口
│   ├── avox_aac/               # AAC 编解码（faac/fdk-aac）
│   ├── avox_ffmpeg/            # FFmpeg 解封装、软解与硬解
│   ├── avox_zlmediakit/        # ZLMediaKit 直播流（RTSP/RTMP）
│   ├── avox_vulkan/            # Vulkan GPU 处理管线
│   ├── avox_freetype/          # FreeType 文字渲染
│   ├── avox_egl/               # EGL 上下文
│   ├── avox_agent/             # 多模态 AI Agent
│   ├── avox_cmd/               # 命令行接口（CLI）
│   ├── avox_windows/           # Windows 平台实现
│   ├── avox_android/           # Android 平台实现
│   ├── avox_apple/             # Apple 平台实现（iOS/macOS）
│   └── avox_linux/             # Linux 平台实现
├── plugins/                   # 动态插件（运行期加载）
│   ├── avox_webrtc/            # WebRTC 集成
│   ├── avox_onnx/              # ONNX 推理
│   ├── avox_opencv/            # OpenCV 图像处理
│   ├── avox_sherpa/            # Sherpa-ONNX 语音识别
│   ├── avox_translation/       # 神经机器翻译
│   └── avox_cv/                # AI 图像修复（水印去除）与通用 YOLO
├── swig/                      # SWIG 多语言绑定
│   ├── csharp/                # C# SDK
│   ├── nodejs/                # Node.js SDK
│   └── python/                # Python SDK
├── platform/                  # 各平台 SDK 封装与 UI 演示
│   ├── windows/ android/ ios/
│   └── avalonia/              # Avalonia 跨平台 C# Demo
├── samples/                   # 功能测试示例
├── glsl/                      # Vulkan GLSL 着色器（.glsl → .spv）
├── cmake/                     # CMake 构建脚本与工具链
├── script/                    # 辅助脚本（下载依赖、打包等）
├── 3rdparty/                  # 第三方依赖源码
├── assets/                    # 资源文件（字体、图片、模型）
├── doc/                       # 项目文档
└── build/                     # 构建输出
```

## 架构概览

```
┌─────────────────────────────────────────────────────────────┐
│                      Application Layer                       │
│           (C# / Java / JS / Python / Swift / ObjC)          │
├─────────────────────────────────────────────────────────────┤
│                      SDK Layer (SWIG)                        │
├─────────────────────────────────────────────────────────────┤
│                       Core Player                            │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐   │
│  │ IO Thread │→ │ Decoder  │→ │ Render   │→ │ Output   │   │
│  │           │  │ Thread   │  │ Thread   │  │          │   │
│  └──────────┘  └──────────┘  └──────────┘  └──────────┘   │
├─────────────────────────────────────────────────────────────┤
│  Data Sources          Decoders           Renders           │
│  ┌─────────────┐      ┌─────────┐        ┌──────────┐      │
│  │ FFmpeg      │      │ HW:     │        │ DX11/12  │      │
│  │ ZLMediaKit  │      │ MediaCodec│       │ Metal    │      │
│  │ WebRTC      │      │ VideoToolbox│     │ Vulkan   │      │
│  │ Device      │      │ SW: FFmpeg│       │ OpenGL ES│      │
│  └─────────────┘      └─────────┘        └──────────┘      │
├─────────────────────────────────────────────────────────────┤
│  Vulkan GPU Pipeline        │        Dynamic Plugins        │
│  (100+ Effects)             │   (WebRTC / ONNX / ASR / ...) │
└─────────────────────────────────────────────────────────────┘
```

## API 说明

- **`create` 前缀** - 返回需要手动释放的对象，调用者持有所有权。
- **`get` 前缀** - 返回托管对象，不要释放。
- **回调** - 用 `addXxxOb()` / `removeXxxOb()` 注册与注销，回调对象释放前先注销。

```cpp
IMediaPlayer* player = createMediaPlayer();          // 用户所有，需 delete
ISurfaceRender* render = player->getSurfaceRender(); // 托管，不释放
```

## 常见问题

**avox 是什么？** 一套跨平台音视频能力库（C++17）：采集 → 硬解 → Vulkan GPU 处理 → 渲染 → 硬编/推流录制，播放器是其中最完整的模块。

**支持哪些平台？** Windows、Android、iOS、macOS、Linux（Linux 的 VAAPI 硬解还在计划中）。

**取流和解码用什么？** 取流与解码分层解耦，内置 FFmpeg（通用格式）、ZLMediaKit（RTSP/RTMP 直播）、WebRTC 三条来路；硬解走 D3D11VA / MediaCodec / VideoToolbox，统一进 Vulkan 零拷贝管线。

**有哪些语言绑定？** C++ 原生；SWIG 生成 C# / Java / Node.js / Python；UI Demo 覆盖 Win32、Android（Java）、iOS（ObjC）、Electron（JS）、Avalonia（C#）。

**可以商用吗？** 开源协议是 AGPLv3，另提供商业授权。

**文档在哪里？** 仓库 [doc/INDEX.md](doc/INDEX.md)；技术系列连载在 [panvox.net/tech/build/](https://panvox.net/tech/build/)。

## 文档

- **[doc/INDEX.md](doc/INDEX.md)** - 文档总索引（模块说明、API、平台集成、构建配置）
- **[doc/reports/README.md](doc/reports/README.md)** - 排查案例集：已定谳案例（根因、修复提交号、可复用判据）
- **[DeveloperGuide.md](DeveloperGuide.md)** - C++ 编码规范与跨 DLL 安全规范
- **[CLAUDE.md](CLAUDE.md)** - 项目结构概览与 AI 辅助开发指引
