# AVOX Code Wiki

> 本 Wiki 是本项目（avplay / AVOX）的代码百科，面向希望快速理解源码结构、模块职责、关键类与运行方式的开发者。
> 所有内容基于当前仓库源码整理，随代码演进请同步维护。

## 文档索引

| 文档 | 内容 |
|------|------|
| [01-项目概览与架构.md](01-项目概览与架构.md) | 项目定位、整体架构、数据流、分层设计 |
| [02-目录结构与模块职责.md](02-目录结构与模块职责.md) | 顶层目录、核心模块、插件模块、平台模块职责 |
| [03-核心类与关键接口.md](03-核心类与关键接口.md) | 公共头文件、核心类、关键函数、模块注册机制 |
| [04-依赖关系.md](04-依赖关系.md) | 第三方库、git 子模块、插件依赖、外部大型库 |
| [05-构建与运行.md](05-构建与运行.md) | 各平台构建、运行方式、测试、发布 |

## 快速速查

- **项目名**：`avplay`（SDK 库名 `avox`，产物 `avox.dll` / `libavox.so` / `libavox.a`）
- **语言/标准**：C++17/20，RAII，智能指针；`.h`(C 接口) / `.hpp`(C++) / `*Export.h`(公共 API)
- **命名空间**：`avox`（`namespace avox {` / `}`）
- **导出宏**：`AVOX_EXPORT`（见 `src/avox/AvoxDef.h`），插件用 `AVOX_PLUGIN_API`
- **平台**：Windows / Android / iOS / Linux / WebAssembly
- **核心能力**：播放器、软/硬解、Vulkan GPU 图像处理（100+ 效果）、WebRTC 推拉流、AI（ASR/TTS/翻译/去水印/YOLO/OCR/Avatar）、Agent 客户端、CLI 工具

## 核心模块一览

| 层级 | 目录 | 一句话职责 |
|------|------|-----------|
| 核心框架 | `src/avox/` | 公共接口 + 播放器/数据源/解码/渲染/封装/字幕/视觉 编排 |
| FFmpeg | `src/avox_ffmpeg/` | FFmpeg 软解/硬解、解封装、转码 |
| ZLMediaKit | `src/avox_zlmediakit/` | RTSP/RTMP 直播流解析与推流（ONVIF） |
| Vulkan | `src/avox_vulkan/` | GPU 图像处理管线 + 跨平台渲染 |
| FreeType | `src/avox_freetype/` | 文字渲染（字体层） |
| 平台层 | `src/avox_windows|android|ios|linux|egl` | 各平台解码/渲染/设备实现 |
| Agent | `src/avox_agent/` | AI Agent（VLM 对话 + skill + 工具闭环） |
| CLI | `src/avox_cmd/` | `avox_cli` 命令行工具（命令逻辑折进 avox.dll） |
| 插件 | `plugins/` | 独立 dll 组件：onnx/cv/sherpa/translation/opencv/webrtc/avatar/ocr/openvino |

## 阅读路径建议

1. 想了解**整体架构与数据流** → 看 [01-项目概览与架构.md](01-项目概览与架构.md)
2. 想找**某个模块在哪个目录、干什么** → 看 [02-目录结构与模块职责.md](02-目录结构与模块职责.md)
3. 想用/改**某个类或接口** → 看 [03-核心类与关键接口.md](03-核心类与关键接口.md)
4. 想加**新功能/新模块**（含插件） → 看 [04-依赖关系.md](04-依赖关系.md) 的插件机制
5. 想**编译/运行/调试** → 看 [05-构建与运行.md](05-构建与运行.md)

## 相关链接

- 官方导航：[doc/INDEX.md](../INDEX.md)
- 项目总览：[CLAUDE.md](../../CLAUDE.md)
- 开发规范：[DeveloperGuide.md](../../DeveloperGuide.md)
- 设计/规划文档：`doc/plan/`、`doc/player/`、`doc/build/`
