# avox / Godot

`platform/godot/` 下包含以下部分（示例工程与使用文档已独立为 [avox-godot](https://github.com/xxxzhou/avox-godot) 仓库）：

| 目录 | 作用 |
|------|------|
| **`plugin/`** | **把 avox 封装给 Godot 用**。将 avox SDK（音视频播放/采集/录制/AI 等）包装成 Godot 4 GDExtension 插件（`avox_godot`），Godot 项目里直接 `MediaPlayer.new()` 即可使用。随 avox 一起构建，产物输出到 avox install 树。 |
| **`tools/`** | **Godot 实测项目（UI 工具箱）**。一个真实 Godot 项目，用来做各种 UI 工具与验证场景（当前是视频播放器 UI 测试）。通过 `deploy_godot.ps1` 把插件部署进来实测。 |
| **`package/`** | **打包脚本**。`pack_godot_tools.ps1`（旧的：免导出整合，拷 171M 编辑器进 Release + `--path` 跑源码）；首选 `script/package/pack_godot.py`（见下，导出发布版，干净可分发）。 |

## plugin/ —— avox 封装

- 源码：`plugin/src/`（`godot_init.cpp` 导出入口 `avox_godot_init`）
- 构建：随 avox 一起（顶层选项 `AVOX_ENABLE_GODOT`，Windows 默认 ON）
- 产物：`avox_godot.dll` 输出到 avox install 树（`build/windows/avplay/install/AMD64/Release/`）
- 部署到任意 Godot 项目：`./plugin/deploy_godot.ps1 -GodotProject <项目>`
  （`.gdextension` + `bin` junction 指向构建输出，免拷贝）
- 详细说明：[`plugin/README.md`](plugin/README.md)

## tools/ —— Godot UI 工具箱

- 场景/脚本：`tools/src/`（`main.gd` / `main.tscn`）
- `tools/addons/` 是部署目标（`deploy_godot.ps1` 用 junction 指向 avox 构建输出），不入库
- 用 Godot 4.3+ 打开 `tools/project.godot` 即可运行

## 打包分发 —— `script/package/pack_godot.py`

把一份**干净、自包含、可分发**的 Godot 工具箱从 avox 构建安装树（`build/.../Release`，只读不动）挑出来拷到另一个 `--out` 目录。镜像 `pack_sdk_windows.py`，复用 `sdk_common`。

- **Godot 引擎走导出发布版**（release 模板，无编辑器，脚本编进 `.pck`），不再是 171M 编辑器 + 散装源码。注：Godot 4.7 的发布模板 exe 本身就有 ~105M（4.x 引擎体积），这是引擎下限，非打包可压缩部分。
- **AI 模型 / 第三方 AI 库（onnx/opencv/openvino/sherpa）默认不打包**，用户用产物里的 `assets/script/fetch_assets.py` 按需下载（沿用 manifest）。
- 产物体积从 Release 的 1.7G → **~260M**（avox 运行时 ~120M，其中 ffmpeg avcodec 占 89M；发布版 exe ~105M；插件壳 + assets ~28M）。

```bash
python script/package/pack_godot.py --out D:\avox_godot_pack   # 导出发布版 (首选)
python script/package/pack_godot.py --no-export               # 逃生舱: 编辑器+源码, 本地测 (不可移植)
python script/package/pack_godot.py --with-thirdparty         # 连 AI 库一起打包 (否则按需下载)
```

**前置**：导出发布版需 Godot 4.7 的 export templates。模板没装时脚本会**自动下载安装**（从 GitHub 官方 `.tpz` 全平台包提取 Windows 部分，~1.2GB 一次性下载，提示确认）；GitHub 慢可用 `--template-url` 换镜像，或手动下 `.tpz` 后 `--godot-template <文件>`。`--no-download` 关闭自动下载，`--no-export` 则完全不导出。

旧的 `package/pack_godot_tools.ps1`（免导出整合：拷编辑器进 Release + `--path` 跑源码）等价于 `--no-export` 的思路，保留作参考。
