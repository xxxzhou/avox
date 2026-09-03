# avox_godot_plugin

avox SDK 的 Godot 4 GDExtension 插件，让 Godot 项目可以直接使用 avox 的音视频播放、设备采集、录制、AI 等能力。

本插件位于 avox 仓库 `platform/godot/plugin/`，作为 avox 的 CMake 目标随 avox 一起构建；运行时部署到 Godot 项目的 `addons/avox_godot/`。

## 依赖

- **avox SDK** — 本仓库 (上级目录)
- **godot-cpp** — Godot 4 C++ 绑定 (`3rdparty/godot-cpp` 子模块)
- **Godot 4.3+** — 用户自行安装的 Godot 编辑器

## 构建

插件是 avox 的 CMake 目标 (顶层选项 `AVOX_ENABLE_GODOT`，Windows 默认 ON)，随 avox 一起构建：

```bash
# 1. 拉取子模块 (含 godot-cpp)
git submodule update --init --recursive

# 2. avox 全量构建 (含插件)
python build_windows.py
```

产物输出到 avox install 树 `build/windows/avplay/install/AMD64/Release/`：

- `avox_godot.dll`（`.gdextension` 是仓库内静态文件 `plugin/avox_godot.gdextension`，不入 Release，避免 junction 部署时被 Godot 重复扫描）

## 部署到 Godot 项目

```powershell
# 用工具脚本部署: 复制 .gdextension + bin junction 指向 avox 构建输出 (免拷贝)
./platform/godot/plugin/deploy_godot.ps1 -GodotProject D:\Work\MyGodotGame
```

或手动：把 `plugin/avox_godot.gdextension` 复制到项目 `addons/avox_godot/`，把 `avox_godot.dll` 及 avox 运行时 dll 放入 `addons/avox_godot/bin/`（`res://addons/avox_godot/bin/...` 引用即此路径）。

## 使用示例

```gdscript
# 播放视频
var player = MediaPlayer.new()
player.url = "rtmp://example.com/live"
player.hard_decode = true
player.play()
$TextureRect.texture = player.get_texture()

# 监听状态
player.state_changed.connect(func(state):
    print("Player state: ", state)
)
```

## 项目结构

```
platform/godot/
├── plugin/                    # GDExtension 插件源码
│   ├── src/
│   │   ├── godot_init.cpp     # 插件入口 (导出符号 avox_godot_init)
│   │   ├── player.h/cpp       # MediaPlayer 节点
│   │   ├── surface.h/cpp      # 视频帧→Godot纹理桥接
│   │   ├── audio.h/cpp        # 音频桥接
│   │   ├── gpu_passthrough.h  # GPU 直通 (Vulkan 跨设备共享)
│   │   └── volk_win32.c       # volk Win32 扩展函数加载
│   ├── demo/                  # 最小示例场景
│   ├── docs/                  # 设计文档
│   ├── CMakeLists.txt
│   └── deploy_godot.ps1
└── tools/                     # Godot 编辑器实测项目
    ├── src/                   # GDScript 场景与脚本
    │   ├── main.gd
    │   └── main.tscn
    ├── addons/                # 部署的插件运行时 (gitignore)
    └── project.godot
```

## 许可

与 avox SDK 保持一致。
