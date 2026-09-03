# device — 枚举设备

Phase 1 — 纯查询命令，枚举系统音视频采集设备。

## 用法

```bash
# 列出所有设备
avox_cli device

# 仅视频设备
avox_cli device -video

# 仅音频设备
avox_cli device -audio

# JSON 格式
avox_cli device -json

# 指定设备 SDK
avox_cli device -video -sdk dshow
```

## 选项

| 选项 | 长选项 | 类型 | 必填 | 说明 | 默认值 |
|------|--------|------|------|------|--------|
| `-video` | | Bool | | 仅视频设备 | 全部 |
| `-audio` | | Bool | | 仅音频设备 | 全部 |
| `-sdk` | `--sdk` | String | | 设备 SDK 类型 | 平台默认 |
| `-json` | | Bool | | JSON 格式输出 | 关闭 |

## SDK 类型 (按平台)

| 平台 | 视频 SDK | 音频 SDK |
|------|----------|----------|
| Windows | dshow, win_capture | wasapi |
| Android | camera2 | aaudio |
| iOS | avcapture | avcapture |
| Linux | v4l2 | pulseaudio |

## 输出示例

**文本格式:**
```
Video Devices:
  [0] HD WebCam (dshow)
      1920x1080 @ 30fps
      1280x720 @ 30fps
  [1] Screen Capture (win_capture)

Audio Devices:
  [0] 麦克风 (Realtek Audio) (wasapi)
  [1] 立体声混音 (Realtek Audio) (wasapi)
```

**JSON 格式:**
```json
{
  "video": [
    {
      "index": 0,
      "name": "HD WebCam",
      "sdk": "dshow",
      "formats": [
        {"width": 1920, "height": 1080, "fps": 30},
        {"width": 1280, "height": 720, "fps": 30}
      ]
    }
  ],
  "audio": [
    {
      "index": 0,
      "name": "麦克风 (Realtek Audio)",
      "sdk": "wasapi"
    }
  ]
}
```

## 对应 SDK API

- `getVideoManager(VDeviceSdk)` → `IVideoManager*` → `enumDevices()`
- `getAudioManager(ADeviceSdk)` → `IAudioManager*` → `enumDevices()`

## 实现要点

1. 根据 `-video` / `-audio` 决定枚举哪些设备
2. 根据 `-sdk` 选择设备 SDK，未指定则使用平台默认
3. Windows 默认: 视频 `dshow`, 音频 `wasapi`
4. 调用 `IVideoManager`/`IAudioManager` 的设备枚举接口
5. 格式化输出设备名称、支持的格式等
