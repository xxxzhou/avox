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
| `rec.hard.encode` | bool / false | 转码 | 转码模式硬编 (仅 TranscodeRecorder 消费) |
| `io.rtsp.transport` | string / - | IO层 | RTSP底层传输 udp/tcp, 与播放器共用 |
| `io.timeout.ms` | int / - | IO层 | IO超时, 与播放器共用 |
| `io.trackready.ms` | int / - | IO层 | Track ready等待超时, 与播放器共用 |

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
