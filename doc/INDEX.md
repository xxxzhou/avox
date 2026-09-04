# avox 项目导航

本文档提供项目各模块的快速导航，帮助快速了解项目结构和功能。

## 项目概述

avox 是跨平台音视频播放器 SDK，支持直播流、本地媒体、硬件解码、Vulkan GPU 图像处理、WebRTC 集成及 AI 功能。

**支持平台：** Windows、Android、iOS、Linux、WebAssembly

**详细文档：** [README.md](../README.md) | [AGENTS.md](../AGENTS.md)

---

## 文档子集 (按领域)

| 子集 | 说明 |
|------|------|
| [player](player/README.md) | 播放器: 核心/时间同步/硬解渲染/数据源/平台集成/问题记录 |
| [webrtc](webrtc/) | WebRTC 集成、编译、编解码器对接、双向通话 |
| [virtualproduction](virtualproduction/README.md) | 虚拟制片: XR 演播/标定/追踪/传输 (来自 aocec 整理同步) |
| [platforms](platforms/) | 平台开发: Android / iOS / Linux (硬解、NDK、Xcode 等) |
| [code-wiki](code-wiki/README.md) | 代码架构 Wiki: 概览/目录/核心类/依赖/构建 |
| [ai](ai/) | AI 功能: inpaint 图像修复、语音转文字 |
| [build](build/) | 构建: 脚本/CMake、Android、FFmpeg、SWIG、wasm |
| [tools](tools/) | 开发工具: VSCode/Wireshark/ffmpeg/git/trae |
| [plan](plan/) | 设计与规划方案 |

---

## 模块索引

### 核心模块

| 模块 | 目录 | 功能 | 文档 | 状态 |
|------|------|------|------|------|
| avox | `src/avox/` | 核心播放器框架 | [Code Wiki 架构](code-wiki/01-项目概览与架构.md) | ✅ |
| avox_player | `src/avox/player/` | MediaPlayer/SourcePlayer 实现 | [Code Wiki 核心类](code-wiki/03-核心类与关键接口.md) | ✅ |
| avox_source | `src/avox/source/` | 数据源 (AVSource/RawSource) | [数据源](player/数据源.md) | ✅ |

### 解码模块

| 模块 | 目录 | 功能 | 文档 | 状态 |
|------|------|------|------|------|
| avox_video | `src/avox/video/` | 视频解码器基类 | [播放器硬解](player/播放器硬解.md) | ✅ |
| avox_audio | `src/avox/audio/` | 音频解码器基类 | - | ✅ |
| avox_ffmpeg | `src/avox_ffmpeg/` | FFmpeg 软解/硬解 | [播放器FFmpeg](player/播放器FFmpeg.md) | ✅ |
| avox_aac | `src/avox_aac/` | AAC 解码 (faad2/fdk-aac) | - | ✅ |

### IO 模块

| 模块 | 目录 | 功能 | 文档 | 状态 |
|------|------|------|------|------|
| avox_zlmediakit | `src/avox_zlmediakit/` | 直播流解析 (RTSP/RTMP) | [ZLMediaKit](player/ZLMediaKit.md) | ✅ |
| avox_muxer | `src/avox/muxer/` | 媒体复用 | [Code Wiki 核心类](code-wiki/03-核心类与关键接口.md) | ✅ |

### 渲染模块

| 模块 | 目录 | 功能 | 文档 | 状态 |
|------|------|------|------|------|
| avox_vulkan | `src/avox_vulkan/` | Vulkan GPU 图像处理管线 | [Code Wiki 模块](code-wiki/02-目录结构与模块职责.md), [多平台GPU共享](player/多平台GPU共享.md) | ✅ |
| avox_freetype | `src/avox_freetype/` | FreeType 文字渲染 | - | ✅ |

### 平台模块

| 模块 | 目录 | 功能 | 文档 | 状态 |
|------|------|------|------|------|
| avox_windows | `src/avox_windows/` | Windows 平台实现 | - | ✅ |
| avox_android | `src/avox_android/` | Android 平台实现 | [Android硬解](platforms/android/Android硬解Vulkan.md) | ✅ |
| avox_ios | `src/avox_ios/` | iOS 平台实现 | [IOS硬解](platforms/ios/IOS硬解Vulkan.md) | ✅ |
| avox_linux | `src/avox_linux/` | Linux 平台实现 | - | ✅ |
| avox_egl | `src/avox_egl/` | EGL 上下文 | - | ✅ |

### WebRTC 模块

| 模块 | 目录 | 功能 | 文档 | 状态 |
|------|------|------|------|------|
| avox_webrtc | `plugins/avox_webrtc/` | WebRTC 集成 (动态插件) | [播放器WebRTC](webrtc/播放器WebRTC.md) | ✅ |

### AI 模块

| 模块 | 目录 | 功能 | 文档 | 状态 |
|------|------|------|------|------|
| avox_cv | `plugins/avox_cv/` | AI 图像修复 (水印去除) + 通用 YOLO | [inpaint](ai/inpaint.md) | ✅ |
| avox_sherpa | `plugins/avox_sherpa/` | Sherpa-ONNX 语音识别 | [sherpa](ai/语音转文字.md) | ✅ |
| avox_translation | `plugins/avox_translation/` | 神经机器翻译 | | ✅ |
| avox_opencv | `plugins/avox_opencv/` | OpenCV 图像处理 / 模板匹配 (图标定位) | [图标模板匹配](../plugins/avox_opencv/doc/图标模板匹配设计.md) | ✅ |
| avox_ocr | `plugins/avox_ocr/` | OCR 文字识别 (PP-OCRv6, 供 Agent 按文字点击) | [OCR识别设计](../plugins/avox_ocr/doc/OCR识别设计.md) | ✅ |

### Agent 模块

| 模块 | 目录 | 功能 | 文档 | 状态 |
|------|------|------|------|------|
| avox_agent | `src/avox_agent/` | AI Agent (VLM 对话 + skill + 工具闭环) | [README](../src/avox_agent/README.md) | ✅ |

### SWIG 绑定

| 语言 | 目录 | 文档 | 状态 |
|------|------|------|------|
| C# | `swig/csharp/` | [swig](build/swig.md) | ✅ |
| Java | `swig/Java/` | [swig](build/swig.md) | ✅ |
| Node.js | `swig/nodejs/` | [swig](build/swig.md) | ✅ |

### 多引擎插件

| 引擎 | 位置 | 文档 |
|------|------|------|
| Godot 4 | `platform/godot/plugin/` | [README](../platform/godot/plugin/README.md) |
| Unreal Engine | `platform/ue/` | [README](../platform/ue/README.md) |
| Unity | `platform/unity/` | [README](../platform/unity/README.md) |

---

## 构建文档

| 文档 | 说明 |
|------|------|
| [构建](build/构建.md) | 构建脚本、CMake 配置 |
| [Android构建](build/Android.md) | Android 构建指南 |
| [FFmpeg构建](build/FFmpeg构建.md) | FFmpeg 编译 |
| [swig](build/swig.md) | SWIG 多语言绑定 |
| [Swig与JS回调](build/Swig与JS回调.md) | nodejs 绑定与 JS 回调 |
| [wasm](build/wasm.md) | WebAssembly 编译 |

---

## 开发工具

| 文档 | 说明 |
|------|------|
| [VSCode卡顿排查](tools/VSCode卡顿排查.md) | VSCode 项目卡顿根因与解法(工作区缓存) |
| [git常用操作](tools/git常用操作.md) | git 常用命令 |
| [Wireshark](tools/Wireshark.md) | 抓包分析 |
| [ffmpeg](tools/ffmpeg.md) | ffmpeg 命令 |
| [trae](tools/trae.md) | trae 使用 |

---

## 关键接口

### 播放器接口

```cpp
// URL 播放器
class IMediaPlayer {
  virtual void open(const char* url) = 0;
  virtual void close() = 0;
  virtual void pause(bool pause) = 0;
  virtual void seek(int64_t pos) = 0;
  virtual void setSpeed(double speed) = 0;
};

// 原始数据播放器
class ISourcePlayer {
  virtual void open() = 0;
  virtual void close() = 0;
  virtual void pushFrame(const YUVFrame& frame) = 0;
  virtual void pushFrame(const AvoxAFrame& frame) = 0;
};
```

### 数据源接口

```cpp
// 编码数据源
class IAVSource {
  virtual bool open(const char* url) = 0;
  virtual void close() = 0;
  virtual ISourceInfo* getSourceInfo() = 0;
};

// 原始数据源
class IRawSource {
  virtual bool open() = 0;
  virtual void close() = 0;
};
```

### 渲染接口

```cpp
// 窗口渲染
class ISurfaceRender {
  virtual void setWindow(void* window) = 0;
  virtual void render(const GpuFrame& frame) = 0;
  virtual void render(const YUVFrame& frame) = 0;
};

// 音频渲染
class IAudioRender {
  virtual void render(const AvoxData& data) = 0;
};
```

---

## 第三方库

| 库 | 目录 | 用途 |
|-----|------|------|
| ZLMediaKit | `3rdparty/ZLMediaKit/` | 直播流服务器和解析 |
| fdk-aac | `3rdparty/fdk-aac/` | AAC 编解码 |
| faad2 | `3rdparty/faad2/` | AAC 解码 |
| freetype | `3rdparty/freetype/` | 字体渲染 |
| sherpa-onnx | `3rdparty/sherpa-onnx/` | 语音识别 |

---

## 示例代码

| 平台 | 目录 |
|------|------|
| 所有平台 | `samples/` |
| 平台封装 | `platform/` |

---

## 快速查找

**按功能查找：**

- 音视频同步 → [播放器时间](player/播放器时间.md)
- 硬件解码 → [播放器硬解](player/播放器硬解.md)
- Vulkan 处理 → [Code Wiki 模块](code-wiki/02-目录结构与模块职责.md)
- WebRTC → [播放器WebRTC](webrtc/播放器WebRTC.md)
- AI 功能 → [AI 模块](ai/)
- GPU 直通/纹理共享 → [多平台GPU共享](player/多平台GPU共享.md)

**按平台查找：**

- Windows → `src/avox_windows/`
- Android → [Android 硬解](platforms/android/Android硬解Vulkan.md)
- iOS → [iOS 硬解](platforms/ios/IOS硬解Vulkan.md)
- Linux → `src/avox_linux/`
- WebAssembly → [wasm](build/wasm.md)

**按开发阶段查找：**

- 新手入门 → [README.md](../README.md)
- 架构理解 → [Code Wiki 架构](code-wiki/01-项目概览与架构.md)
- 构建 → [构建](build/构建.md)
- 扩展功能 → [AI 模块](ai/)
