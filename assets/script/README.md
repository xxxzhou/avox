# assets/script/ — 运行时资源获取

本目录随 SDK 发布（与 `fonts`/`images`/`config` 同级，CMake 统一复制），提供插件运行时资源（AI 模型 + 运行时动态库）的统一获取：`fetch_assets.py` 读 `assets_manifest.json` 作为**唯一入口**。

> 原则：**清单（JSON）是唯一权威**，脚本只读不硬编码。加资源 = 加一条 JSON，不改脚本。
> 只收**运行时**资源（模型 + `.dll`/`.so`）；不收 `.lib`/`.a` 静态库、不收 sentencepiece（静态链入）。

---

## 文件

| 文件 | 角色 |
|------|------|
| `assets_manifest.json` | 清单（唯一权威）：每项 = 需要什么 / 下载链接 / 目标目录 / 解压方式 / 适用平台 |
| `fetch_assets.py` | 统一脚本，读 JSON 执行。仅依赖 Python 标准库 |

## 快速开始

```bash
python assets/script/fetch_assets.py --list                      # 列出全部及就绪状态（自动检测平台）
python assets/script/fetch_assets.py --list --platform linux     # 换平台视角
python assets/script/fetch_assets.py --interactive               # 交互多选 (编号 / a 全选 / 回车)
python assets/script/fetch_assets.py --select sherpa_sense_voice,inpaint_lama
python assets/script/fetch_assets.py --plugin avox_sherpa --all   # 按插件过滤
python assets/script/fetch_assets.py --list --json               # 结构化输出 (供 avox_cmd 调用)
```

选项：`--root <部署根>` · `--platform <windows|linux|macos>` · `--force` · `--dry-run` · `--hf-mirror` · `--no-color`

---

## 清单结构（JSON schema）

顶层 `version` / `sources` / `conventions` / `items[]`。每个 item：

| 字段 | 必填 | 说明 |
|------|------|------|
| `id` | ✅ | 唯一标识（`--select` 用） |
| `name` `plugin` | ✅ | 显示名 / 所属 `avox_<name>` |
| `type` | ✅ | `model` \| `library` |
| `method` | ✅ | `download` \| `script` \| `manual` \| `build` |
| `dest` | ✅ | 目标目录，相对 `--root` |
| `files` | download | `[{url, name?, size_mb?, sha256?}]` |
| `archive` | 可选 | `{format: zip|tarbz2|targz, flatten}` |
| `verify_files` | 可选 | 就绪判断 + 下载后校验；无则显示 `—` |
| `platforms` | 库项 | 平台分支，见下；模型项省略 = 全平台 |
| `command` | script | 要转发的命令 |
| `source`/`source_ref` | manual/build | 来源说明 |
| `note` | 可选 | 备注 |

### method 四种

| method | 行为 | 用例 |
|--------|------|------|
| `download` | HTTP 下载 + 解压 + 校验 | 模型、windows 运行时 DLL |
| `script` | 转发现有专用脚本（.exe 解压 / 量化等） | 复杂库导出 |
| `manual` | 无源/需训练，只打印指引不下载 | OCR、AOT-GAN、linux/ios 库 |
| `build` | 源码编译产物，提示走 build 脚本 | sherpa-onnx DLL |

---

## 平台维度

**模型项平台无关**，顶层 `files`。**运行时库平台相关**，用 `platforms` 按平台给各自 `files`，spec 可覆盖 `method`：

```jsonc
{ "id": "onnxruntime_dll", "dest": "plugins", "method": "download",
  "platforms": {
    "windows": { "files": [{"url":".../onnxruntime.dll"}], "verify_files": ["onnxruntime.dll"] },
    "linux":   { "method": "manual", "note": "未收录，从官方取 libonnxruntime.so" },
    "android": { "method": "manual", "note": "静态库，无运行时独立库" }
  } }
```

`--platform`（默认按 `sys.platform` 自动检测）选择；不含该平台则标"不适用"并 SKIP。

---

## 数据源

| 仓库 | 角色 | 形态 |
|------|------|------|
| [`xxxzhou/avox_model`](https://github.com/xxxzhou/avox_model) | AI 模型 | inpaint=git-lfs 单文件；stt=release zip（扁平）；translation=已量化 int8 LFS；ocr=det/rec onnx + 字典 (LFS)。**未收 AOT-GAN** |
| [`xxxzhou/avc_library`](https://github.com/xxxzhou/avc_library) | 预编译库 | 见下 |

> git-lfs 文件用 `https://github.com/<repo>/raw/<branch>/<path>` 取真实内容（自动 302 到 media）；勿用 `raw.githubusercontent.com`（返回 lfs pointer）。

`avc_library` 平台覆盖（`3rdparty/library/<平台>/<库>/`）：

| 平台 | onnxruntime | opencv |
|------|-------------|--------|
| windows | `onnxruntime.dll`（动态）✓ | `opencv_world4130.dll`（动态）✓ |
| android | `libonnxruntime.a`（静态） | 未收录 |
| linux / ios | 未收录 | 未收录 |

> 完整开发库（含 `.lib`/头文件）仍需 `git clone avc_library ../avc_library` 供 CMake 构建；本清单只取**运行时 DLL**。
> sherpa-onnx / sentencepiece 不在 avc_library：sherpa DLL 靠 `build_windows.py` 编译，sentencepiece 静态链入。

---

## 运行时落点

所有 `dest` 相对 `--root`（默认仓库根）：

| 资源 | dest | 运行时查找 |
|------|------|-----------|
| 模型 | `assets/models/<功能>/` | avox.dll 同级 `assets/models/`（Android=APK assets，iOS=bundle） |
| 运行时 DLL | `plugins/` | avox.dll 同级 `plugins/`（LoadLibraryEx 搜索） |

⚠️ **下运行时 DLL 时 `--root` 必须指向部署根**（`avox.dll`/`avox_cli.exe` 所在目录，如 `build/.../install/AMD64/Release`），**勿用仓库根**（会污染源码 `plugins/`）。下模型用仓库根即可（构建时 CMake 复制 stt/translation 到输出）。

---

## 当前清单（12 项）

| id | 插件 | 方式 | 说明 |
|----|------|------|------|
| `inpaint_lama` | avox_cv | download | LaMa 修复模型 198MB |
| `inpaint_yolo` | avox_cv | download | YOLO26-Seg 训练好 .onnx 90MB |
| `inpaint_aotgan` | avox_cv | manual | avox_model 未收录 |
| `ocr_ppocrv6` | avox_ocr | download | PP-OCRv6 det+rec+字典 |
| `sherpa_zh_en` | avox_sherpa | download | 流式中英 zip 168MB |
| `sherpa_sense_voice` | avox_sherpa | download | SenseVoice+VAD zip 153MB |
| `translation_opus_mt` | avox_translation | download | 已量化 int8，4 文件 |
| `quality_realesrgan_x4v3` | avox_vulkan | download | Real-ESRGAN x4v3 ONNX 4.6MB（普通 git，非 LFS）|
| `onnxruntime_dll` | avox_onnx | download/manual | win 实际，其余 manual |
| `openvino_dll` | avox_openvino | download/manual | win 实际（11 dll+cache.json），其余 manual |
| `opencv_dll` | avox_opencv | download/manual | win 实际，其余 manual |
| `sherpa_onnx_dll` | avox_sherpa | build | 编译产物 |

---

## 新增一条资源

1. 编辑 `assets_manifest.json` 加一条 item（模型顶层 `files`；库用 `platforms`）。
2. `python assets/script/fetch_assets.py --select <id> --dry-run` 验证 URL，再实下。
3. 脚本不用改。

---

## avox_cmd 集成

avox_cmd `assets` 子命令经内置 python 执行器(SubprocessRunner spawn 机器 python)调用：

```
fetch_assets.py --list --json
  → {"platform":..,"items":[{id,name,plugin,type,method,dest,platforms,applicable,ready}]}

fetch_assets.py --select id1,id2 --root <部署根> --platform <plat> [--json]
  → 退出码 0=全成 / 1=有失败；--json 回 {results:[{id,action,ok,msg}]}
```

`applicable`/`ready` 字段直接驱动 UI。

---

## 已知限制

- **AOT-GAN**：avox_model 未收录，`manual`，需自备。
- **sherpa-onnx DLL**：avc_library 未收录，`build`（`build_windows.py` 编译）。
- **GPU/CUDA**：`onnxruntime_providers_shared.dll` + CUDA 运行时不在自动获取范围（CPU 够用）。
- **linux/ios 运行时库**：avc_library 未收录，`manual` 指引官方源；收录后填 `platforms.<plat>` URL 即可。
- **android/ios 不跑此脚本**：走 APK/assets 打包，`manual` 标注仅作清单完整性。
