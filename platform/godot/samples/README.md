# avox Godot Samples

`avox_godot` 插件的官方示例工程 —— 4 个自包含场景，覆盖播放、直播、采集、语音。每个场景的 UI 全部由代码构建（`.tscn` 只挂根节点），脚本本身就是最直接的 API 文档。

## 场景

| 场景 | 演示的节点 | 说明 |
|---|---|---|
| **01 本地播放** | `MediaPlayer` | 文件选择、硬解 + GPU 直通开关、播放/暂停/停止、进度条 seek、音量、倍速、`get_media_info()` |
| **02 直播流** | `MediaPlayer` | RTMP / HLS / RTSP 地址输入 + 预设（含公网 HLS 测试流可直接验证）、状态/帧率/媒体信息 |
| **03 相机采集** | `SourcePlayer` + `DeviceManager` | 设备枚举、实时预览到 `TextureRect` |
| **04 语音字幕** | `MicCapture` + `SttNode` | 麦克风采集 → 流式语音识别实时上屏（partial 灰色 / final 白色） |

## 运行前置

1. Godot **4.3+**，渲染后端 **Forward+（Vulkan）**（GPU 直通依赖 Vulkan）
2. 已构建 avox SDK（`python build_windows.py`，顶层选项 `AVOX_ENABLE_GODOT` 默认开）
3. 部署插件到本工程（二选一）：

```powershell
# 方式一: junction 部署 (开发推荐, 免拷贝)
./platform/godot/plugin/deploy_godot.ps1 -GodotProject <本工程路径>

# 方式二: 手动把 avox_godot.dll + avox 运行时 dll 拷进 addons/avox_godot/bin/
```

`addons/avox_godot/avox_godot.gdextension` 已在工程内，部署后直接用 Godot 打开 `project.godot` 运行。

## 注意

- **04 语音字幕**需要 STT 模型 `sherpa_zh_en`（约 168MB）：用 avox 安装树里的 `assets/script/fetch_assets.py` 下载，或直接用 `tools/` 工具箱的语音输入页面完成下载。模型缺失时场景会通过 `error` 信号提示，不会崩溃。
- **02 直播流**的 RTMP/RTSP 预设是占位地址，需要换成你自己的拉流地址；公网 HLS 测试流（mux.dev）可直接验证网络播放。
- 状态码含义：`0 空闲 / 1 打开中 / 2 就绪 / 3 播放中 / 4 暂停 / 5 跳转 / 6 缓冲 / 7 已停止 / 8 播放完成`（对应 `AvoxPlayer.h` 的 `PlayerState`）。

## 最小代码

```gdscript
var player = MediaPlayer.new()
add_child(player)
player.play("res://movie.mp4")   # 或任意 http/rtmp/rtsp 地址
$TextureRect.texture = player.get_texture()
```

## 工具

- `tools/screenshot_gen.gd` —— 商店截图生成器（真实 Vulkan 渲染逐场景抓帧，01 场景自动播 `assets/video/avox_electron.mp4`）：
  `godot --path . --resolution 1280x720 -s res://tools/screenshot_gen.gd`，产物在 `screenshots/`（工程内, 已 gitignore）。
