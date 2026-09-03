# asr — 语音识别

Phase 2 — 流式语音识别，支持文件和实时麦克风输入。

## 用法

```bash
# 识别音频文件
avox_cli asr -i audio.wav

# 识别并输出 SRT
avox_cli asr -i audio.wav -o subtitle.srt

# 实时麦克风识别
avox_cli asr -live

# 指定语言
avox_cli asr -i audio.wav -lang zh
```

## 选项

| 选项 | 长选项 | 类型 | 必填 | 说明 | 默认值 |
|------|--------|------|------|------|--------|
| `-i` | `--input` | String | | 输入音频文件 | - |
| `-o` | `--output` | String | | 输出 SRT/文本文件 | stdout |
| `-live` | | Bool | | 实时麦克风识别 | 关闭 |
| `-lang` | | String | | 语言 | 自动 |
| `-model` | | String | | 模型路径 | 内置 |

## 对应 SDK API

- `ISubtitle`: `enableAsr()`
- `avox_sherpa` 模块: 流式 ASR
- 参考: `samples/vulkantest/sherpaTest.cpp`

## 实现要点

1. `-live` 模式: 创建设备采集源 + ASR 处理
2. 文件模式: 读取音频文件 + ASR 处理
3. 实时模式输出识别文本到终端
4. `-o` 输出为 SRT 字幕格式
5. Ctrl+C 优雅退出
