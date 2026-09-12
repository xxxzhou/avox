# script/ — 开发期辅助脚本

本目录是**开发期**脚本，均**不随 SDK 发布**。

> 运行时资源获取（`fetch_assets.py` + `assets_manifest.json`）在 **[`../assets/script/`](../assets/script/)**，随 SDK 发布。

## 目录内容

| 目录 | 用途 |
|------|------|
| `godot/` | Godot 离线工具（`idle_bake.py` avatar 待机微动烘焙 → `../platform/godot/tools/src/avatar/idle_bake.json`） |
| `torrent/` | torrent 播放分步基准（`torrent_bench.py`，驱动 `../samples/functest/torrentbench.cpp`；见 `../plugins/avox_torrent/README.md`） |
| `ffmpeg/` | 从源码构建 FFmpeg（`build_ffmpeg.py`，多 flavor 白名单，`--deploy` 换库进 3rdparty） |
| `smb2/` | 构建 libsmb2 预编译库（`build_windows.py`，静态 /MT，产物进 avc_library） |
| `webrtc/` | 构建 WebRTC（`.bat`/`.sh`/`.ps1`，Windows/Android/iOS） |
| `onnx/` | 下载 ONNX Runtime 开发库（各平台，含 `.lib`） |
| `opencv/` | 下载 OpenCV 开发包 |
| `openvino/` | 提取 OpenCV 开发包（`extract_openvino.py`） |
| `realesrgan/` | 超分模型导出（`export_onnx.py`） |
| `sherpa/` | 下载语音模型（旧脚本） |
| `translation/` | 下载并量化翻译模型（需 WSL） |
| `inpaint/` | 水印去除模型训练 / 数据集脚本 |
| `package/` | 打包 SDK、上传 release（含 `pack_godot.py`） |
| `verify/` | 线上问题排查/验证脚本集（`parse_dump.py` minidump 解析、seek 卡顿、ZLM 泄漏等） |
| `testenv/` | 功能测试本地流源 + 统一判定行汇总（`push_streams.py`、`collect_verdicts.py`；配套 headless 用例见 `../platform/godot/tools/tests/`） |
| `dsh/` | dsh 互通性验证（`verify_dsh_interop.mjs`、`zstd_token_sum.py`） |

## 旧分散下载脚本（向后兼容，部分过时）

下列脚本仍可独立运行，但运行时资源获取**推荐用 [`../assets/script/fetch_assets.py`](../assets/script/fetch_assets.py) 统一入口**（新机制已用 `avox_model`/`avc_library` 源替代它们的下载逻辑）：

| 目录 | 脚本 | 状态 |
|------|------|------|
| `inpaint/` | `download_lama_models.py`、`download_yolo26_seg.py`（下 .pt 训练权重）、`yolo_to_onnx.py` | 仍可用；新机制直接下训练好的 .onnx |
| `sherpa/` | `down_zh_en_model.py`、`down_sense_voice.py` | 仍可用；新机制改用 avox_model release |
| `translation/` | `download_and_export.py`（需 WSL 量化） | 仍可用；新机制直接取已量化文件 |
| `onnx/` | `down_onnxruntime_{windows,linux,ios,android}.py` | 仍可用（下完整开发库含 `.lib`）；新机制只取运行时 DLL |
| `opencv/` | `download_opencv.py` | 仍可用（`.exe` 解压）；新机制直接取 DLL |

> `inpaint/` 下训练/数据集脚本（`generate_watermark_dataset.py`、`train_yolo_seg.sh`、`mask_to_yolo_label.py` 等）与资源下载无关，照常使用。
