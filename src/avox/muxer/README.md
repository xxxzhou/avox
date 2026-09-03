# avox/muxer 模块

音视频封装与录制模块，负责将编码后的音视频数据封装输出到文件或网络流，支持转封装录制和转码录制。

## 类层次

```
IMediaMuxer (公共接口)
  └─ MediaMuxer (编码数据封装: H264/H265/AAC → MP4/RTSP/...)
       ├─ IOMuxer (IO 封装线程: 队列消费 + 格式写入)
       │    └─ 子类实现 (FFmpegMuxer, ZlmMuxer 等，在 avox_ffmpeg/avox_zlmediakit 模块)
       │
       └─ RawMuxer (原始数据封装: YUV/PCM → 编码 → 封装)
            ├─ VideoStream (视频编码流: YUV/GPU → H264/H265)
            │    └─ VideoEncoder (视频编码器)
            └─ AudioStream (音频编码流: PCM → AAC)
                 └─ AudioEncoder (音频编码器)

IRecorder (公共接口)
  ├─ StreamRecorder (转封装录制: AVSource → MediaMuxer，不解码不编码)
  └─ TranscodeRecorder (转码录制: AMediaSource → Vulkan处理 → RawMuxer，解码+处理+编码)
```

## 类说明

| 类 | 文件 | 说明 |
|---|---|---|
| **MediaMuxer** | MediaMuxer.hpp | 编码数据封装器，接收 H264/H265/AAC 包，通过 IOMuxer 写入文件或推流，支持 Annexb/AVCC 格式检测和包拆分合并 |
| **RawMuxer** | RawMuxer.hpp | 原始数据封装器，在 MediaMuxer 基础上整合 VideoStream/AudioStream 编码器，输入 YUV/GPU/PCM 原始数据，编码后封装输出 |
| **IOMuxer** | IOMuxer.hpp | IO 封装线程，音视频编码包入队列后由 RunTask 线程取出封装，子类实现具体格式写入 (FFmpeg/ZLMediaKit) |
| **VideoStream** | VideoStream.hpp | 视频编码流，管理 VideoEncoder，将 YUVFrame/GpuFrame 编码为 H264/H265 包，支持硬编/软编切换 |
| **AudioStream** | AudioStream.hpp | 音频编码流，管理 AudioEncoder，将 AvoxAFrame 编码为 AAC 包，支持重采样格式转换 |
| **AVEncoder** | AVEncoder.hpp | 编码器基类，定义 `onPreEncoder`/`flush`/`onClose` 接口，子类实现具体编码逻辑 |
| **StreamRecorder** | StreamRecorder.hpp | 转封装录制器，将 AVSource 输出的编码包直接转封装保存，不解码不编码，速度取决于 IO |
| **TranscodeRecorder** | TranscodeRecorder.hpp | 转码录制器，解码后经 Vulkan 图像处理（缩放/水印等），重新编码保存；解码与编码通过队列解耦，队列满时反压 IO 层 |

## 接口说明

| 接口 | 文件 | 说明 |
|---|---|---|
| **IMediaMuxer** | AvoxMuxer.h | 媒体封装器公共接口，提供 `setHardEncode`、`setVideoCodec`、`setAudioCodec`、`open`、`close` |
| **IRecorderOb** | AvoxMuxer.h | 合成器观察者（统一用于 IMediaMuxer 和 IRecorder），回调 `onStateChange`、`onProgress`、`onIoError`、`onEncodeError`、`onComplete` |
| **IMuxerOb** | Muxer.hpp | 封装器内部观察者，回调 `onMuxerOpen`、`onMuxerClose`，用于上层设置音视频描述 |
| **IMuxerContext** | Muxer.hpp | 封装器上下文，使 VideoStream/AudioStream 能访问所属的 MediaMuxer |
| **IRecorder** | AvoxMuxer.h | 录制器公共接口，提供 `setIoPlan`、`setMuxerType`、`open`、`close`、`getState` |
| **IEncoderOb** | AVEncoder.hpp | 编码器观察者，回调 `onPacket`，编码完成后输出编码包 |

## C 接口

| 函数 | 说明 |
|---|---|
| `addMuxerOb(muxer, ob)` / `removeMuxerOb(muxer, ob)` | 添加/移除封装器观察者（IRecorderOb） |
| `createRecorder()` | 创建转封装录制器 (StreamRecorder) |
| `createTranscodeRecorder()` | 创建转码录制器 (TranscodeRecorder) |
| `addRecorderOb(recorder, ob)` / `removeRecorderOb(recorder, ob)` | 添加/移除录制器观察者 |

## 枚举类型

| 枚举 | 说明 |
|---|---|
| **MuxerType** | 封装类型：`other`、`ffmpeg`、`zlmediakit`、`onvif` |
| **IoPlan** | IO 方案：`none`、`zlmediakit`、`ffmpeg` |
| **EncodeResult** | 编码结果：负值=不可恢复错误，0=成功，正值=可继续尝试 |
| **RecorderState** | 合成状态（统一用于 IMediaMuxer 和 IRecorder）：`none` → `opening` → `recording` → `completed`（唯一终态：手动停止/自然完成/出错均为 completed，原因看 `onComplete`/`onIoError` 回调） |

## 数据流

### 编码数据封装 (MediaMuxer)

```
AVSource.onPacket(AvoxPacket)
  → MediaMuxer.pushPacket()
    → 包拆分/合并/格式检测
    → IOMuxer.pushPacket() → 队列
      → RunTask 线程取出 → onPushPacket() → FFmpeg/ZLMediaKit 写入
```

### 原始数据封装 (RawMuxer)

```
YUVFrame/GpuFrame → RawMuxer.pushFrame() → VideoStream.encoderFrame() → VideoEncoder → onPacket() → IOMuxer
AvoxAFrame         → RawMuxer.pushFrame() → AudioStream.encoderFrame() → AudioEncoder → onPacket() → IOMuxer
```

### 转封装录制 (StreamRecorder)

```
AVSource(拉流) → onPacket() → MediaMuxer(写文件/推流)
```

### 转码录制 (TranscodeRecorder)

```
AMediaSource(拉流+解码) → onVideoFrame/onGpuFrame/onAudioFrame
  → 帧队列 (解耦解码与编码线程)
    → RunTask 线程取出
      → VkVideoRender(图像处理: 缩放/水印等)
      → RawMuxer(编码+封装)
```

## 编码器名称常量

| 常量 | 说明 |
|---|---|
| `AVOX_FF_H264_ENCODER` / `AVOX_FF_H265_ENCODER` | FFmpeg 软编 (libx264/libx265) |
| `AVOX_FFVULKAN_H264_ENCODER` / `AVOX_FFVULKAN_H265_ENCODER` | FFmpeg Vulkan 硬编 |
| `AVOX_FFDX11_H264_ENCODER` / `AVOX_FFDX11_H265_ENCODER` | FFmpeg DX11 硬编 |
| `AVOX_FFVAAPI_H264_ENCODER` / `AVOX_FFVAAPI_H265_ENCODER` | FFmpeg VAAPI 硬编 (Linux) |
| `AVOX_ANDROID_H264_ENCODER` / `AVOX_ANDROID_H265_ENCODER` | Android MediaCodec 硬编 |
| `AVOX_IOS_H264_ENCODER` / `AVOX_IOS_H265_ENCODER` | iOS VideoToolbox 硬编 |

## 使用示例

### 编码数据封装 (播放器推流)

```cpp
auto muxer = mediaplayer->getMuxer(false);  // false = 编码数据直接封装
muxer->setMuxerType(MuxerType::ffmpeg);
addRecorderOb(muxer, myObserver);
// 在 onMuxerOpen 回调中设置音视频描述
muxer->open("rtsp://output_url");
// 从 AVSource 获取编码包推入
muxer->pushPacket(packet);
muxer->close();
```

### 原始数据封装 (YUV/PCM 编码推流)

```cpp
auto muxer = mediaplayer->getMuxer(true);  // true = 需要编码器
muxer->setHardEncode(true);
muxer->setVideoCodec(VCodecId::h265);
muxer->setMuxerType(MuxerType::ffmpeg);
muxer->open("output.mp4");
// 推入原始帧
rawMuxer->pushFrame(yuvFrame);
rawMuxer->pushFrame(audioFrame);
muxer->close();
```

### 转封装录制

```cpp
auto recorder = createRecorder();
recorder->setIoPlan(IoPlan::ffmpeg);
recorder->setMuxerType(MuxerType::ffmpeg);
addRecorderOb(recorder, myObserver);
recorder->open("rtsp://input_url", "output.mp4");
// 自动拉流转封装保存
recorder->close();
```

### 转码录制

```cpp
auto recorder = createTranscodeRecorder();
recorder->setIoPlan(IoPlan::ffmpeg);
addRecorderOb(recorder, myObserver);
// 设置输出格式（可选，不设置则与源相同）
recorder->open("input.mp4", "output.mp4");
// 自动解码 → Vulkan 处理 → 重新编码保存
recorder->close();
```
