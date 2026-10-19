# record — 录制/转存流

支持转封装(remux)和转码(transcode)两种录制模式，shell 实时显示进度，转码模式可叠加 PTS 字体。

## 用法

```bash
# 转封装录制 (默认, 不解码直接转存)
avox_cli record -i rtsp://192.168.1.100/live -o output.mp4
avox_cli record -i rtsp://... -o out.mp4 -t 60

# 转码录制 (解码→图像处理→编码, 可叠效果/水印)
avox_cli record -i rtsp://... -o out_tc.mp4 -tc -t 60

# 默认输出路径 (自动生成到 records/ 目录)
avox_cli record -i rtsp://... -t 30

# 指定 IO 方案
avox_cli record -i rtsp://... -o out.mp4 -io ffmpeg

# 回放/NVR录像倍速拉流下载 (仅zlmediakit IO, 是否全帧率取决于设备)
avox_cli record -i rtsp://... -o out.mp4 -io zlmediakit -speed 4

# 硬编码 (转码模式)
avox_cli record -i rtsp://... -o out.mp4 -tc -hard

# 只抽取音频/视频 (去掉另一轨)
avox_cli record -i rtsp://... -o audio_only.mp4 -novideo
avox_cli record -i rtsp://... -o video_only.mp4 -noaudio
```

## 选项

| 选项 | 长选项 | 类型 | 必填 | 说明 | 默认值 |
|------|--------|------|------|------|--------|
| `-i` | `--input` | String | ✅ | 输入源 URL | - |
| `-o` | `--output` | String | | 输出文件路径 | 自动生成到 `records/` 目录 |
| `-t` | `--duration` | Int | | 录制时长(秒), 0=无限 | 0 |
| `-tc` | | Boolean | | 转码录制 (解码→处理→编码) | false (转封装) |
| `-novideo` | | Boolean | | 关闭视频轨 (只录音频) | false |
| `-noaudio` | | Boolean | | 关闭音频轨 (只录视频) | false |
| `-io` | | String | | IO方案: auto/ffmpeg/zlmediakit | auto |
| `-hard` | | Boolean | | 硬编码 (转码模式有效) | false |
| `-speed` | | Number | | RTSP拉流倍速 (仅zlmediakit IO, 点播/回放源有效) | 1.0 |
| `-q` | `--quality` | Int | | 编码质量档 0..100 (越大越清晰、文件越大) | 60 |
| `-b:v` | `--bitrate` | Int | | 视频目标码率 kbps (0=按分辨率自动) | 0 |
| `-rc` | | String | | 码率控制: quality/vbr/cbr | quality |
| `-preset` | | String | | 编码预设: speed/balanced/quality | balanced |
| `-gop` | | Int | | 关键帧间隔秒 | 3 |
| `-profile` | | String | | profile: compat/main/high | high |

视频编码旋钮仅 `-tc` 转码路生效(转封装不编码)。**逐项独立**, 只给想改的那一项即可,
未给的项保持缺省档。缺省档 = 质量优先 + 质量档60 + 均衡预设 + high profile + 3秒关键帧;
1080p30 硬编实测约 2.4 Mbps(改前是 186 Mbps 的码率目标, 落到 10.6 Mbps 的产物)。

```bash
# 要更小的文件(实测 1080p30: 质量档 30 ≈ 760KB / 6秒, 85 ≈ 3.8MB)
avox_cli record -i in.mp4 -o out.mp4 -tc -hard -q 40
# 压到指定码率(给 -b:v 即自动转 VBR; 要严格恒定码率再加 -rc cbr)
avox_cli record -i in.mp4 -o out.mp4 -tc -hard -b:v 1500
avox_cli record -i in.mp4 -o out.mp4 -tc -hard -rc cbr -b:v 1500
# 老设备兼容 / 更密的 seek 粒度
avox_cli record -i in.mp4 -o out.mp4 -tc -hard -profile compat -gop 1
```

## 编码参数: 接口分两条路

**常用旋钮走结构体** `IRecorder::setVideoEncodeParam(const VideoEncodeParam&)`
(定义在 `AvoxMuxer.h`, open 前设置, 整包替换)。上层 UI 直接填结构体即可, 不必
拼 key 字符串; SWIG 绑定(C#/Java/Node)同样可见。逐项独立, 缺省值 = 不指定:

| 字段 | 缺省 | 说明 |
|------|------|------|
| `rateMode` | `inherit` | `quality` 质量定档 / `vbr` / `cbr` |
| `quality` | `-1`(→60) | 质量档 0..100 |
| `bitrateKbps` | `0`(自动) | 目标码率 kbps; 给了值就按码率收口(VBR), 除非另指定 `rateMode` |
| `preset` | `inherit`(→均衡) | `speed` / `balanced` / `quality` |
| `gopSeconds` | `0`(→3) | 关键帧间隔秒(不常用, 另有 option 入口) |
| `profile` | `inherit`(→high) | `compat` / `main` / `high`(不常用, 另有 option 入口) |

**不常用项留 option 入口**: `rec.encode.gop` / `rec.encode.profile`
(异步/远程配置路径用; 走 `getOption()->setInt()`)。注意结构体是整包替换 ——
若两条路都用, 先调 `setVideoEncodeParam` 再设 option, 否则会被结构体覆盖。

旋钮在各平台编码器的落地情况(空 = 该车道没有这个档位, 会被忽略而不是报错):

| 旋钮 | NVENC | QSV | AMF | VAAPI | MF (Win) | MediaCodec (Android) | VideoToolbox (Apple) |
|------|-------|-----|-----|-------|----------|----------------------|----------------------|
| 码控模式 | vbr+cq / vbr / cbr | ICQ / VBR / CBR | cqp / vbr_peak / cbr | CQP / VBR / CBR | quality / u_vbr / cbr | CQ / VBR / CBR | Quality / AverageBitRate(+DataRateLimits) |
| 质量档 | cq | ICQ 档 | qp_i/p/b | qp | quality 0~100 | quality 0~100 | Quality 0.0~1.0 |
| 预设 | p1~p7 | veryfast~veryslow | speed~high_quality | 1~7(越高越快) | — | — | 只有"速度优先"开关 |
| profile | ✓ | ✓ | ✓ | ✓ | — (其 profile 是数值枚举, 且实测对产物无影响) | ✓ | ✓ |

软编(libx264/libx265, AGPL 渠道)三项全支持: CRF / ABR / 严格 CBR + 预设 + profile。

## 输出示例

```
Recording: rtsp://192.168.1.100/live -> records/record_remux_20260706_143025.mp4
  Mode: remux | IO: ffmpeg
  Press Ctrl+C to stop.
  Progress: 45% (00:00:27/00:01:00)
```

无 duration 时显示 PTS:
```
  Progress: PTS 00:00:27
```

## 转码模式 PTS 字体叠加

`-tc` 模式下，若编译启用 `AVOX_ENABLE_FREETYPE`，自动在画面左上角叠加 PTS 时间文字，用于确认录制进度与画面对应关系。

## 对应 SDK API

- `createRecorder(bool bTranscode)` → `IRecorder*`
  - `false`: 转封装 (passthrough), 不解码直接转存
  - `true`: 转码 (transcode), 解码→图像处理→编码
- `IRecorder`: `open(inputUrl, outputFile)`, `close()`, `getState()`
- `IRecorder::getSurfaceRender()`: 转码模式获取渲染器, 可叠加效果/水印/字体
- `setIoPlan()`: 设置 IO 方案
- `setMuxerType()`: 设置封装格式 (默认 ffmpeg)
- `setVideoCodec(VCodecId)` / `setAudioCodec(ACodecId)`: open前设置编码, 设 `none` 丢弃对应轨只录另一轨
- `IRecorderOb`: `onStateChange(RecorderState, RecorderState)`, `onProgress(RecorderProgress)`, `onIoError()`, `onEncodeError()`, `onComplete()`
- `RecorderProgress`: `currentTimeMs`, `totalTimeMs`
- `RecorderState`: `none` → `opening` → `recording` → `completed`（唯一终态）
- `IRecorder::getOption()`: 返回参数设置器 (`IOption`), open前设置生效。key 定义:

| key | 类型/默认 | 消费 | 说明 |
|-----|-----------|------|------|
| `io.rtsp.speed` | double / 1.0 | IO层 | RTSP拉流倍速(PLAY带Scale头), 仅 `zlmediakit` IO 生效(ffmpeg IO 打warn); 点播/NVR回放源有效, 直播源无效; 是否全帧率取决于设备(华为4x全帧率, 8x/16x抽帧) |
| `rec.hard.decode` | bool / false | 转码 | 转码模式硬解 (仅 TranscodeRecorder 消费) |
| `rec.hard.encode` | bool / true | 转码 | 转码模式硬编 (仅 TranscodeRecorder 消费) |
| `rec.encode.gop` | int / 3 | 编码 | 关键帧间隔秒。常用项(码控模式/质量/码率/预设)不在 option 里, 走 `setVideoEncodeParam` |
| `rec.encode.profile` | int / -1 | 编码 | profile: 0=compat 1=main 2=high; -1=按缺省(high) |
| `io.rtsp.transport` | string / - | IO层 | RTSP底层传输 udp/tcp, 与播放器共用 |
| `io.timeout.ms` | int / - | IO层 | IO超时, 与播放器共用 |
| `io.trackready.ms` | int / - | IO层 | Track ready等待超时, 与播放器共用 |
| `io.startFrac` | double / 0.0 | IO层 | 起播定位比例(0~0.9): 读循环前按 duration×frac 定位到目标前最近关键帧, 抽帧/续播免从 0 起播; 仅索引容器(mp4/mov/mkv/webm/avi/rm)生效, 失败降级从 0; 与播放器共用 |

## 实现要点

1. 创建 `IRecorder` (由 `-tc` 决定转码/转封装)
2. 设置 IO 方案和封装格式
3. 转码模式: `getSurfaceRender()` → `setOffSurface()` 离屏渲染, 可选 `enableRenderFont()` 叠加 PTS
4. 注册 `IRecorderOb` 观察者, 缓存 `RecorderProgress` 供主循环读取
5. `open(inputUrl, outputFile)` 开始录制
6. 主循环: 检查 state/时长/进度, 每 200ms 刷新 shell 进度行 (`\r` 覆盖)
7. 进度显示: 有 totalTimeMs 显示百分比, 无则显示 PTS, 再无显示帧数
8. `-t` 指定时长, 到达后自动停止; 0 则无限录制直到 Ctrl+C
9. Ctrl+C 信号处理: 优雅调用 `close()` 保存文件

## 默认输出路径

- 目录: `getAvoxPath() + "/records"` (avox.dll 所在目录的 records 子目录)
- 文件名: `record_tc_YYYYMMDD_HHMMSS.mp4` (转码) 或 `record_remux_YYYYMMDD_HHMMSS.mp4` (转封装)
- `-o` 指定时使用指定路径, 自动创建父目录
