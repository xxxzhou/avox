# AVOX 核心模块

跨平台音视频 SDK 的核心实现，定义公共接口（根目录 `.h`）并提供内部实现（子目录 `.hpp/.cpp`）。所有公共类型位于 `avox` 命名空间，通过 `AVOX_EXPORT` 导出。

## 目录结构

| 目录 | 功能 | 关键类 |
|------|------|--------|
| **audio/** | 音频解码、渲染、重采样、编码、语音识别 | AudioDecoder, AudioRender, AudioReshaper, AudioStt, AudioEncoder |
| **codec/** | H264/H265 NAL 解析 (SPS/PPS/VPS) | H264Parse, H265Parse, H26XNalReader, H26XHelper |
| **layer/** | GPU 渲染管线图 (PipeGraph) | PipeGraph, VInputLayer, VOutputLayer, VLayer |
| **module/** | 基础设施：JSON 配置、配置键常量、模块管理、环形缓冲、观察者模式 | JsonOption, OptionKey, AvoxManager, ModuleMgr, Ringbuffer, Observer |
| **muxer/** | 音视频封装、录制、转码 | MediaMuxer, RawMuxer, StreamRecorder, TranscodeRecorder |
| **player/** | 播放器编排、A/V 同步、轨道管理 | MediaPlayer, SourcePlayer, AVDecoder, Clock, VideoTrack, AudioTrack |
| **source/** | 媒体数据源 (FFmpeg/ZLMediaKit/WebRTC/设备采集) | AVSource, RawSource, DeviceSource, PacketBuf |
| **subtitle/** | 字幕解析、ASR 语音识别、翻译 | SubtitleView, SubtitleFile, SubtitleAsr, SrtParser |
| **video/** | 视频解码、渲染、窗口管理、编码、图像处理 | VideoDecoder, VideoRender, WindowRender, VideoEncoder |

## 公共接口 (根目录 .h)

### 核心基础

| 文件 | 说明 |
|------|------|
| **AvoxDef.h** | 基础宏：命名空间、`AVOX_EXPORT`、`AVOX_HAS_CPP17`；兼容 OpenCV 的深度/通道类型宏 (`AVOX_CV_*`) |
| **AvoxVersion.h** | 构建版本：commit hash、分支名、构建时间 |
| **AvoxBuffer.h** | 数据缓冲：`IAvBuffer`、`AvoxData`、`AvoxPacket`（编码帧）、`PackType` |
| **AvoxBase.h** | 公共基础层（跨域通用类型 + 接口）：`IP4Address`/`IP4Endpoint`、`getAvoxRunDir`(运行目录)；翻译 `ITranslator`/`Language`/`ModelLevel`/`createTranslator`/`checkModelLoad`；Python `IPyRunner`/`getPyRunner`；配置 `IOption`/`IOptionOb`/`ArgType`/`createJsonOption`（改名链 AvoxNet.h→AvoxCommon.h→AvoxBase.h，并入原 AvoxModel/AvoxPython/AvoxOption） |
| **AvoxLog.h** | 日志系统：`LogLevel`、`ILogOb`、C API `logMsg` |
| **AvoxTime.h** | 时间系统：`FrameRate`、`Timecode`、`Timespan`、高精度时钟 API |
| **AvoxMath.h** | 3D 数学库：向量/矩阵/四元数/姿态（右手坐标系） |
| **AvoxImage.h** | 图像类型：`ImageType`、`ImageFormat`、`IImageBuffer`、`IEncodeType`/`IEncodeConfig`、图像 I/O（load/save/resize/crop/base64） |

> 配置键常量 (播放器/IO/日志 key) 仅 .cpp 引用，已移至 `module/OptionKey.hpp`，不在导出根目录。

### 数据类型

| 文件 | 说明 |
|------|------|
| **AvoxVideo.h** | 视频：`YuvType`、`YUVFrame`、`YUVFormat`、`VideoDesc`、`ColorSpaceDesc`、YUV I/O 及与 `IImageBuffer` 的桥接函数（依赖 AvoxImage.h） |
| **AvoxAudio.h** | 音频：`AudioFormat`、`AudioDesc`、`IAudioRender`、ADTS 头构造 |
| **AvoxCodec.h** | 编解码：`ACodecId`/`VCodecId`、`VTrackDesc`/`ATrackDesc`、`DecoderParams` |

### 主要接口

| 文件 | 说明 |
|------|------|
| **AvoxSource.h** | 数据源：`IAVSource`(编码流)、`IRawSource`(原始帧)、`ISourceInfo`、设备管理接口 |
| **AvoxPlayer.h** | 播放器：`IMediaPlayer`(URL 播放)、`ISourcePlayer`(设备播放)、`PlayerState`、`IMediaPlayerOb`；字幕 `ISubtitle`/`AsrMode`(SRT/ASR/翻译) |
| **AvoxMuxer.h** | 封装录制：`IMediaMuxer`、`IRecorder`、`IRecorderOb`、`MuxerType` |
| **AvoxLayer.h** | GPU 渲染：`IPipeGraph`/`IPipeNode`(管线图)、`ISurfaceRender`、`IVInputLayer`/`IVOutputLayer` |
| **AvoxVision.h** | 视觉 AI：`IYoloDetector`(检测/分类)、`IWatermarkRemoval`/`InpaintMode`(水印去除)、特征/颜色/方向/地图匹配检测等 |
| **AvoxAgent.h** | Agent 工具：`IAgentTool`(name/description/parametersJson/execute)、`IToolResult`/`ToolOutcome`（execute 返回堆对象，调用方消费后 delete；STL-free 不跨 C 导出边界） |

### 聚合头文件

| 文件 | 说明 |
|------|------|
| **AvoxCore.h** | 聚合核心头文件（导出伞头，常用 .h 一并 include） |
| **Avox.hpp** | 便捷 include-all（.cpp 使用）；含内部 `AndroidEnv`（仅 `__ANDROID__`，不导出） |
| **AvoxNode.hpp** | 节点参数模板层：`IParamet<T>` 及各效果类型别名 |

## 数据流

### URL 播放 (MediaPlayer)

```
IAVSource (FFmpeg/ZLMediaKit)
    │ AvoxPacket (编码帧)
    ▼
IMediaPlayer ──→ AudioTrack / VideoTrack
    │                    │
    ▼                    ▼
ADecoderTask        VDecoderTask
    │                    │
    ▼                    ▼
AvoxAFrame           YUVFrame / GpuFrame
    │                    │
    ▼                    ▼
IAudioRender     ISurfaceRender ──→ IPipeGraph (GPU 效果链)
                                        │
                                        ▼
                                   IVInputLayer → VLayer[] → IVOutputLayer
```

### 设备采集 (SourcePlayer)

```
IVideoSource / IAudioSource (设备原始帧)
    │
    ▼
ISourcePlayer
    │
    ├──→ ISurfaceRender (直接渲染)
    ├──→ IAudioRender
    └──→ IMediaMuxer (可选编码录制)
```

### 封装录制 (Muxer/Recorder)

```
原始帧 / AvoxPacket
    │
    ▼
IMediaMuxer (编码+封装)  /  IRecorder (流录制)
    │
    ▼
FFmpeg / ZLMediaKit / ONVIF 输出
```

## 架构原则

- **接口/实现分离**：根目录 `.h` 定义抽象接口，子目录 `.hpp/.cpp` 提供实现
- **观察者模式**：主要接口均配有 Observer（`IMediaPlayerOb`、`IAVSourceOb`、`IRecorderOb` 等）
- **配置传播**：`IOption` 基于 JSON 的键值配置在管线中传递，对象创建时通过 `optionCopy` 获取初始配置，运行时通过 `optionUpdate`/`optionChange` 同步变更
- **PipeGraph GPU 管线**：`IPipeGraph`/`IPipeNode` 构成节点式渲染图，支持 Anime4K、水印、LUT、翻转、缩放等 GPU 效果
- **跨平台**：Windows(DX11/WASAPI)、Android(MediaCodec/NDK)、iOS(VideoToolbox/AudioUnit)、Linux、WebAssembly

## 子目录详细文档

- [player/README.md](player/README.md) - 播放器架构与轨道绑定
- [source/README.md](source/README.md) - 数据源线程模型
- [subtitle/README.md](subtitle/README.md) - 字幕/ASR/翻译模块
- [codec/READMA.md](codec/READMA.md) - H264/H265 解析参考
