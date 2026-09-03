# avox 沉浸式悬浮播放器 (tools/src/mediaplayer)

Godot 播放器界面工具,半透明悬浮控制条 + 鼠标静止自动隐藏 + 单一强调色。
设计稿见 [`docs/播放器界面设计.md`](../../../docs/播放器界面设计.md)。

## 运行

```powershell
# 前提: addons/avox_godot 已部署
./platform/godot/plugin/deploy_godot.ps1 -GodotProject platform/godot/tools

# 打开项目跑 (Godot 4.3+)
godot --path platform/godot/tools

# 指定播放地址 (cmdline 首个参数, `--` 之后)
godot --path platform/godot/tools -- "D://Back/美好02.mp4"
# 直播/网络流
godot --path platform/godot/tools -- "rtmp://example.com/live"
```

不传参则用 `main.gd` 里的 `DEFAULT_URL`。`HARD_DECODE` 可切硬解(DX11)/软解。

## 快捷键

| 键 | 动作 |
|---|---|
| `Space` | 播放/暂停 |
| `←` / `→` | 后退 / 前进 5s |
| `Shift + ←/→` | 后退 / 前进 60s |
| `↑` / `↓` | 音量 ±10% |
| `M` | 静音 |
| `F` | 全屏 |
| `Esc` | 退出全屏 |

## 交互

- **顶部菜单栏**「文件 / 播放 / 视图」:文件 → 打开文件…(系统文件对话框)/ 打开直播源…(输入 `rtmp://`、`rtsp://`、`http(s)://` 地址)/ 退出;播放 → 播放/暂停、进退 5s;视图 → 全屏。
- **拖放播放**:把视频/音频文件直接拖进窗口即播放。
- **自动隐藏**:鼠标静止 3s(播放中)→ 顶部菜单栏 + 底部状态栏一起淡出、光标隐藏;移动/按键即回。
- 暂停/缓冲/拖进度条/悬停菜单时不自动隐藏。
- 进度条三段:轨道 / 已缓冲 / 已播(accent);拖拽松手才 seek,不卡顿。
- 直播流(`duration ≤ 0`):时间显示 `● 直播`,进度条禁用。

## 文件

```
src/mediaplayer/
├── main.gd        # 主控制脚本(代码构建全部 UI, 接 MediaPlayer API)
├── main.tscn      # 最小场景, 主脚本入口
└── scrub_bar.gd   # 自绘进度条(轨道/已缓冲/已播 + 点击拖拽)
```

## 已知待扩展

- **已缓冲水位**:现 MediaPlayer 无 `get_buffered()`,缓冲时把"已缓冲"顶到播放头近似;
  精确值需插件加 `get_buffered()`(见设计稿 §9 扩展点)。
- 倍速无 `get_speed()`,UI 自存选中态。
