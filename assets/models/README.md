# AI模型文件

本项目使用的 AI 模型文件存放在 `assets/models/` 目录下。

## 目录结构

```
assets/models/
├── stt/                    # 语音识别模型 (sherpa-onnx)
│   ├── zh-en/              # 中英流式识别模型
│   └── sense-voice/        # 多语言离线识别模型 (中/英/日/韩/粤)
├── translation/            # 翻译模型
│   └── opus-mt-ja-zh/      # 日语→中文翻译模型
├── inpaint/                # 水印检测/修复模型
│   ├── lama.onnx           # LaMa 图像修复模型
│   ├── yolo11x_watermark.pt  # YOLO11x 水印检测模型
│   └── owlv2/              # OWLv2 开放词汇检测模型
└── onnx/                   # ONNX Runtime 库
```

---

## 1. 语音识别 (sherpa-onnx)

sherpa-onnx 支持流式和离线语音识别。

### 模型说明

| 模型 | 用途 | 特点 | 路径 |
|------|------|------|------|
| zh-en | 中英文流式识别 | 低延迟，返回中间结果 | `stt/zh-en/` |
| sense-voice | 多语言离线识别 | 高精度，自动语言检测，带标点，支持翻译 | `stt/sense-voice/` |

### 获取脚本

推荐用统一脚本 [`assets/script/fetch_assets.py`](../script/fetch_assets.py)（说明见 [`../script/`](../script/)）：

```bash
python assets/script/fetch_assets.py --list                  # 列出全部及就绪状态
python assets/script/fetch_assets.py --plugin avox_sherpa --all   # 下载 sherpa 模型
```

或用旧脚本直接下（仍可用）：

```bash
python script/onnx/down_onnxruntime_android.py   # ONNX Runtime Android 静态库 (开发库, 含 .lib)
python script/sherpa/down_zh_en_model.py          # 中英文流式 STT 模型
python script/sherpa/down_sense_voice.py          # SenseVoice 多语言离线模型
```

---

## 2. 水印检测/修复 (Inpaint)

用于水印检测和图像修复的 AI 模型。

### 模型放置路径

`assets/models/inpaint/`

### 下载脚本

```bash
# 下载 LaMa 修复模型 (水印消除)
python script/inpaint/download_lama_models.py

# 下载 YOLO11x 水印检测模型 (推荐，已训练)
python script/inpaint/download_yolo11x_watermark.py

# 下载 OWLv2 开放词汇检测模型 (无需训练，文本提示检测)
python script/inpaint/download_owlv2.py
```

### 模型说明

| 模型 | 用途 | 大小 | 格式 | 下载脚本 |
|------|------|------|------|----------|
| LaMa | 图像修复 (水印消除) | ~45MB | .onnx | `download_lama_models.py` |
| YOLO11x Watermark | 水印检测 | ~138MB | .pt | `download_yolo11x_watermark.py` |
| OWLv2 | 开放词汇检测 | 1.5-5GB | .bin | `download_owlv2.py` |

### YOLO 转 ONNX

YOLO 模型需要转换为 ONNX 格式才能用于 C++ ONNX Runtime 推理。

```bash
# 转换 YOLO 模型为 ONNX 格式
python script/inpaint/yolo_to_onnx.py yolo11x_watermark.pt

# 指定输出路径和图像尺寸
python script/inpaint/yolo_to_onnx.py yolo11x_watermark.pt -o watermark.onnx -s 1280
```

转换后模型放置在 `assets/models/inpaint/` 目录下。

### 使用说明

- **YOLO**: 需要先训练才能检测特定水印，泛化能力弱。使用前需转换为 ONNX
- **OWLv2**: 可通过文本提示如 "watermark logo" 直接检测，无需训练

---
