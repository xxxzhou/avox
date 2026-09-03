# transcode — 转码录制

Phase 2 — 转码录制，支持重新编码视频和音频。

## 用法

```bash
# 转码录制
avox_cli transcode -i input.mp4 -o output.mp4 -vcodec h264 -acodec aac

# 指定编码参数
avox_cli transcode -i input.avi -o output.mp4 -vcodec h265 -acodec opus -b:v 4000k

# 硬件编码
avox_cli transcode -i input.mp4 -o output.mp4 -hwencode

# 转码指定时长
avox_cli transcode -i input.mp4 -o output.mp4 -t 60
```

## 选项

| 选项 | 长选项 | 类型 | 必填 | 说明 | 默认值 |
|------|--------|------|------|------|--------|
| `-i` | `--input` | String | ✅ | 输入源 | - |
| `-o` | `--output` | String | ✅ | 输出文件 | - |
| `-vcodec` | | String | | 视频编码: h264/h265 | h264 |
| `-acodec` | | String | | 音频编码: aac/opus/pcm | aac |
| `-b:v` | | String | | 视频码率 (如 4000k) | 自动 |
| `-b:a` | | String | | 音频码率 | 自动 |
| `-hwencode` | | Bool | | 硬件编码 | 关闭 |
| `-t` | `--duration` | Int | | 转码时长 (秒) | 全部 |

## 对应 SDK API

- `createRecorder(true)` → `IRecorder*` (transcode 模式)
- `IMediaMuxer`: `setVideoCodec()`, `setAudioCodec()`, `setHardEncode()`
- 码率设置: 通过 `getOption()` 配置

## 实现要点

1. 创建 `IRecorder` (transcode 模式)
2. 配置视频/音频编码器和码率
3. 如果 `-hwencode` 则调用 `setHardEncode(true)`
4. 注册观察者输出转码进度
5. 支持进度条显示
