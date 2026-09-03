# avox_cli — avplay 命令行工具

类似 ffmpeg 的统一命令行工具，通过参数组合调用 avplay SDK 的各种能力。

可执行文件名: `avox_cli` (Windows: `avox_cli.exe`)

## 设计目标

- **单一入口**: 一个 `avox_cli` 可执行文件，通过子命令区分功能
- **ffmpeg 风格**: `-i input -o output` 风格的参数，熟悉、直觉
- **组合能力**: 播放+录制、解码+AI处理、采集+推流 等可自由组合
- **跨平台**: Windows / Linux / macOS / Android(Termux)

## 命令格式

```
avox_cli <子命令> [全局选项] [子命令选项]
```

## 子命令一览

| 子命令 | 功能 | 对应 SDK API | 详细文档 |
|--------|------|-------------|----------|
| `device` | 枚举设备 | `IVideoManager`, `IAudioManager` | [doc_device.md](doc/doc_device.md) |
| `play` | 播放音视频 | `IMediaPlayer` | [doc_play.md](doc/doc_play.md) |
| `record` | 录制/转存流 | `IRecorder` (passthrough) | [doc_record.md](doc/doc_record.md) |
| `transcode` | 转码录制 | `IRecorder` (transcode) | [doc_transcode.md](doc/doc_transcode.md) |
| `inpaint` | AI 去水印 | `IWatermarkRemoval` | [doc_inpaint.md](doc/doc_inpaint.md) |
| `translate` | 翻译文本 | `ITranslator` | [doc_translate.md](doc/doc_translate.md) |
| `asr` | 语音识别 | `ISubtitle` (ASR) | [doc_asr.md](doc/doc_asr.md) |
| `webrtc` | WebRTC 推拉流 | `IRtcPlayer` | [doc_webrtc.md](doc/doc_webrtc.md) |
| `image` | 图片处理 | `createImageBuffer`, `resizeImage` | [doc_image.md](doc/doc_image.md) |
| `agent` | AI 对话 | `IAgentClient` | [doc_agent.md](doc/doc_agent.md) |
| `vulkan` | Vulkan 信息 | `canVulkan` | [doc_vulkan.md](doc/doc_vulkan.md) |

## 全局选项

```
-loglevel <level>    日志级别: quiet/error/warning/info/verbose/debug
-version             显示版本
-help                显示帮助
-json                JSON 格式输出 (device 等信息类命令)
```

## 目录结构

```
src/avox_cmd/                # 命令逻辑 (经 add_sub_path 折进 avox.dll)
├── ArgParser.hpp           # 参数解析器
├── ArgParser.cpp
├── CmdRegistry.hpp         # 子命令注册表
├── CmdRegistry.cpp
├── CmdExecute.h            # avox_cli 进程内入口 cmdExecute (仅 cli 用)
├── CmdExport.cpp           # cmdExecute 实现
├── CmdExport.h             # 对外语言转发头 (SWIG/其它语言, 预留)
├── commands/               # 各子命令实现 (折进 avox.dll)
│   ├── CmdDevice.h / .cpp
│   └── CmdPlay.h / .cpp
├── cli/                    # avox_cli 薄壳 (独立 exe, AVOX_ENABLE_CLI 时编译)
│   ├── CMakeLists.txt      # add_executable avox_cli
│   └── main.cpp            # 一行: cmdExecute(argc, argv)
├── doc/                    # 设计文档
│   ├── doc_architecture.md # 框架设计
│   └── doc_*.md            # 各命令设计 (device/play/record/...)
└── README.md               # 本文件 (总览)
```

## 构建集成

命令逻辑 (ArgParser/CmdRegistry/各 Cmd) 经 `add_sub_path` 折进 `avox.dll`;
`cli/` 与 `tests/` 是独立 exe, 仅 `AVOX_ENABLE_CLI` 时编译。见 `src/CMakeLists.txt`:

```cmake
add_sub_path(avox_cmd AVOX_HEADER AVOX_SOURCE)          # 命令逻辑折进 avox.dll
add_sub_path(avox_cmd/commands AVOX_HEADER AVOX_SOURCE)
if(AVOX_ENABLE_CLI)
    add_subdirectory(avox_cmd/cli)    # avox_cli 薄壳 exe
endif()
```

开关在顶层 `CMakeLists.txt`:

```cmake
option(AVOX_ENABLE_CLI "build avox_cli tool" ON)
```

构建命令:

```bash
cmake -DAVOX_ENABLE_CLI=ON ...
```

## 实现阶段

**Phase 1 — 基础框架 + 核心命令:**
- [ ] ArgParser + CommandRegistry 框架 → [doc_architecture.md](doc/doc_architecture.md)
- [ ] `device` — 枚举设备，纯查询
- [ ] `play` — 核心播放功能
- [ ] `record` — 核心录制功能

**Phase 2 — 转码 + AI:**
- [ ] `transcode` — 转码录制
- [ ] `inpaint` — AI 去水印
- [ ] `asr` — 语音识别
- [ ] `translate` — 翻译

**Phase 3 — 高级功能:**
- [ ] `webrtc` — WebRTC 推拉流
- [ ] `image` — 图片处理
- [ ] `agent` — AI 对话
- [ ] `vulkan` — Vulkan 信息
