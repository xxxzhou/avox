# avox/source 模块

数据源模块，管理音视频数据的输入、解析、时间戳对齐和设备管理。

## 设计规则

1. **流初始化放在数据流中**：所有流初始化在数据流线程执行，避免外部调用复杂性，确保初始化与数据流在同一线程，数据属性变化（如分辨率）能立即重新初始化。

## 类层次

```
BaseSource (源基类，管理轨道描述)
  ├─ AVSource (编码数据源: H264/H265/AAC)
  │    └─ 子类实现 (FFmpegSource, ZlmSource 等，在 avox_ffmpeg/avox_zlmediakit 模块)
  │
  └─ RawSource (原始数据源: YUV/GPUTexture/PCM)
       ├─ DeviceSource (设备源: 摄像头/麦克风)
       │    ├─ AudioSource (音频设备)
       │    └─ VideoSource (视频设备)
       │
       └─ AMediaSource (文件/网络流解码源)
```

## 类说明

| 类 | 文件 | 说明 |
|---|---|---|
| **BaseSource** | BaseSource.hpp | 源基类，管理音视频轨道描述 (`VTrackDesc`/`ATrackDesc`)，提供 `disableVideo`/`disableAudio` 控制轨道处理 |
| **AVSource** | AVSource.hpp | 编码数据源基类 (H264/H265/AAC)，实现 PTS 对齐、包拆分合并、配置帧管理，子类实现具体 IO 协议 |
| **RawSource** | RawSource.hpp | 原始数据源基类 (YUV/GPUTexture/PCM)，用于转发处理后的原始数据到编码器或渲染器 |
| **DeviceSource** | DeviceSource.hpp | 设备源，组合 `AudioSource` 和 `VideoSource`，输出设备直出的 YUV/PCM 数据 |
| **AMediaSource** | AMediaSource.hpp | 文件/网络流解码源，封装 `AVSource` + `VideoDecoder` + `AudioDecoder`，输出解码后的 YUV/GPU/PCM 帧 |
| **AudioSource** | AudioSource.hpp | 音频设备源，支持 3A 处理 (回声消除/噪声抑制/自动增益)，通过 `AudioProcess` 实现 |
| **VideoSource** | VideoSource.hpp | 视频设备源，管理视频格式列表，处理分辨率变化检测 |
| **PacketBuf** | PacketBuf.hpp | 数据包缓冲区，支持 Annexb/AVCC 格式检测、NALU 类型解析、包合并拆分 |
| **DeviceManager** | DeviceManager.hpp | 设备管理器模板，提供设备枚举、查找、刷新功能，支持音频/视频设备管理 |

## 接口说明

| 接口 | 文件 | 说明 |
|---|---|---|
| **IAVSource** | AvoxSource.h | 编码数据源接口，提供 `open(url)`、`seek`、`position`、`duration` 等播放控制 |
| **IRawSource** | AvoxSource.h | 原始数据源接口，提供 `open()`、`close()`、`bOpening()` 等基础控制 |
| **ISourceInfo** | AvoxSource.h | 源信息接口，提供 `videoSize`、`audioSize`、`getVideoDesc`、`getAudioDesc`、`canSeek` |
| **IAVSourceOb** | AvoxSource.h | 编码源观察者，回调 `onReady`、`onPacket`、`onSyncPts`、`onError`、`onComplete` |
| **IRawSourceOb** | AvoxSource.h | 原始源观察者，回调 `onReady`、`onVideoFrame`、`onGpuFrame`、`onAudioFrame` |
| **IVideoSource** | AvoxSource.h | 视频设备接口，提供 `getDeviceName`、`getDeviceId`、`open`、`close` |
| **IAudioSource** | AvoxSource.h | 音频设备接口，同上 |
| **IVideoManager** | AvoxSource.h | 视频设备管理器接口，提供 `getDevice`、`findDevice`、`getDeviceCount`、`refreshDevices` |
| **IAudioManager** | AvoxSource.h | 音频设备管理器接口，同上 |

## PTS 对齐逻辑

`AVSource::alignPacketPts` 实现音视频时间戳对齐：

1. **视频基准**：以第一个 I 帧的 PTS 为基准时间
2. **音频基准**：有视频时以视频 I 帧后的音频包为基准；无视频时以第一个音频包为基准
3. **同步检查**：音视频基准时间差 < 5 秒时对齐，> 5 秒时各播各的
4. **跳变检测**：检测 PTS 跳变，自动重设基准时间
5. **异常修复**：拦截极小异常 PTS/DTS (< -1000000000)，防止硬解器出错

## 数据包处理

`AVSource::processVideo` 处理视频包：

1. **格式检测**：自动检测 Annexb/AVCC 格式
2. **包拆分**：拆分组合包 (配置帧+I帧、多个I帧、多个P帧)
3. **包合并**：合并同 PTS 的 NALU 单元
4. **配置帧管理**：缓存 SPS/PPS/VPS，确保每个 I 帧前发送配置帧

## 枚举类型

| 枚举 | 说明 |
|---|---|
| **AVSourceMode** | 源模式：`none`、`local`(本地文件)、`live`(直播)、`downLive`(下载直播) |
| **RawSourceType** | 原始源类型：`none`、`device`(设备)、`media`(媒体)、`WebRTC` |
| **AVError** | 错误码：`netTimeout`、`netShutdown`、`deviceError`、`endOfFile` 等 |
| **ADeviceSdk** | 音频设备 SDK：`wasapi`(Windows)、`android`、`ios` |
| **VDeviceSdk** | 视频设备 SDK：`win_mf`、`win_capture`、`and_ndkcamer2`、`ios_avf` |

## 工具函数

| 函数 | 文件 | 说明 |
|---|---|---|
| `getAVErrorStr(error)` | Source.cpp | 获取错误码的字符串描述 |
| `getVideoManager(sdk)` | Source.cpp | 获取视频设备管理器 |
| `getAudioManager(sdk)` | Source.cpp | 获取音频设备管理器 |
| `checkAvccPacket(data, size)` | PacketBuf.hpp | 检查是否为 AVCC 格式包 |
| `addConfigPacket(configs, data, codecId)` | PacketBuf.hpp | 添加/更新配置帧到缓存列表 |

## 使用示例

### 打开编码源 (通过 MediaPlayer)

```cpp
// MediaPlayer 内部创建 AVSource 子类实例
auto player = createMediaPlayer();
player->open("rtsp://example.com/stream");
// onReady 回调后可获取轨道信息
```

### 打开设备源

```cpp
auto videoMgr = getVideoManager(VDeviceSdk::win_mf);
auto device = videoMgr->getDevice(0);
DeviceSource source;
source.setVideoSource(device);
source.open();
// onReady 回调后开始接收 YUV 帧
```

### 打开解码源

```cpp
AMediaSource source;
source.setUri("file.mp4");
source.setHardDecode(true);
source.open();
// onReady 回调后开始接收解码后的 YUV/GPU/PCM 帧
```
