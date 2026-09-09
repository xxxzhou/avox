# CV 模块 (avox_cv)

集中收纳依赖 onnx + opencv 的 CV 能力。本插件注册两类工厂到 `AvoxManager`:

- `watermarkRemovalHub.create("inpaint")` → 水印去除 (见下文)
- `yoloDetectorHub.create("yolo")` → 通用 Ultralytics YOLO 检测/分类 (`IYoloDetector`, 类名/任务/尺寸从模型元数据读取)

## 功能

- **YOLO26-Seg 检测** - 使用 YOLO26-Seg 实例分割模型精确定位水印，输出精确分割掩码
- **LaMa / AOT-GAN 修复** - AI 图像修复去除水印 (支持模型切换)

---

## 目录结构

```
D:\Work\data\inpaint_data\
├── sources/                    # 源数据 (只读，程序不修改)
│   ├── images/                 # 原始无水印图片
│   │   ├── train_val/          # 训练+验证用图片
│   │   └── test/               # 测试集专用图片 (独立)
│   └── watermarks/             # 水印模板图片 (透明PNG)
│       ├── train_val/          # 训练+验证水印 (A组)
│       │   ├── text_short/
│       │   ├── text_long/
│       │   ├── text_chinese/
│       │   ├── copyright/
│       │   ├── overlay/
│       │   └── external/
│       └── test/               # 测试集水印 (B组，独立)
│
├── images/                     # 生成的带水印图片
│   ├── train/                  # 训练集 (6000张)
│   ├── val/                    # 验证集 (800张)
│   └── test/                   # 测试集 (800张)
│
├── masks/                      # 二值分割掩码
│   ├── train/
│   ├── val/
│   └── test/
│
├── labels/                     # YOLO 分割标签 (多边形坐标)
│   ├── train/
│   ├── val/
│   └── test/
│
└── dataset.yaml                # YOLO 数据集配置文件
```

### 数据集配置

| 数据集 | 数量 | 图片来源 | 水印来源 |
|--------|------|----------|----------|
| 训练集 | 5,000 | train_val | A组 |
| 验证集 | 800 | train_val | A组 |
| **测试集** | **800** | **test (独立)** | **B组 (独立)** |

### 类型分布

| 类型 | 占比 | 训练集 | 说明 |
|------|------|--------|------|
| text_short | 20% | 1000 | 短文字 (动态生成) |
| text_long | 15% | 750 | 长文字 (模板组合) |
| text_chinese | 10% | 500 | 中文水印 (动态组合) |
| copyright | 5% | 250 | 版权信息 |
| **文字合计** | **50%** | **2,500** | |
| overlay | 15% | 750 | 半透明图标 |
| external | 35% | 1750 | 品牌 Logo |
| **非文字合计** | **50%** | **2,500** | |

文字水印从 `watermark_vocabulary.txt` 词汇库动态组合生成，覆盖 7000+ 种可能。

### 透明度分布

| 透明度 | 占比 | 说明 |
|--------|------|------|
| 0.0-0.1 | 12.5% | 不透明 (最易检测) |
| 0.1-0.2 | 12.5% | |
| 0.2-0.3 | 12.5% | |
| 0.3-0.4 | 12.5% | |
| 0.4-0.5 | 12.5% | 中等 |
| 0.5-0.6 | 12.5% | |
| 0.6-0.7 | 12.5% | |
| 0.7-0.8 | 12.5% | 透明 (难检测) |

---

## 模型

### YOLO26-Seg 模型 (按 ModelLevel 切换)

| ModelLevel | 模型文件 | 参数量 | 大小 | 速度 | 适用场景 |
|------------|----------|--------|------|------|----------|
| `mini` | `yolo26n-seg_watermark.onnx` | ~3M | ~25MB | 最快 | 实时视频处理 |
| `base` | `yolo26m-seg_watermark.onnx` | ~23M | ~90MB | 中等 | 通用场景 (推荐) |
| `high` | `yolo26x-seg_watermark.onnx` | ~45M | ~150MB | 最慢 | 高精度需求 |

### 修复模型 (支持切换)

| 模型 | 文件 | 大小 | 说明 |
|------|------|------|------|
| LaMa Base | `lama_base.onnx` | 198 MB | 图像修复 (默认) |
| LaMa Mini | `lama_mini.onnx` | 198 MB | 轻量版 |
| AOT-GAN | `aotgan-onnx-float/aotgan.onnx` | ~50 MB | 速度更快 |

**模型切换**:

```cpp
// 方式1: 通过 API
inpaint->setInpaintMode(InpaintMode::aotgan);  // 使用 AOT-GAN
inpaint->setInpaintMode(InpaintMode::lama);    // 使用 LaMa (默认)

// 方式2: 通过配置
InpaintConfig config;
config.modelType = InpaintMode::aotgan;
inpaint->setConfig(config);
```

模型路径: `assets/models/inpaint/`

### 模型下载

```bash
python script/inpaint/download_lama_models.py
python script/inpaint/download_yolo26_seg.py base   # 下载 base 模型
python script/inpaint/download_yolo26_seg.py all    # 下载全部
```

---

## 快速开始

### 1. 生成训练数据

```bash
# 一站式生成: 下载图片 + 生成水印 + 生成数据集
python script/inpaint/generate_watermark_dataset.py
```

数据生成流程:
1. 从 `real_images_urls.txt` 下载原始图片 → `sources/images/`
2. 从 `watermark_images_urls.txt` 下载图片水印 → `sources/watermarks/`
3. 生成文字水印 (含中文) → `sources/watermarks/*/`
4. 按分布生成训练集 → `images/train/` + `masks/train/`
5. 按分布生成验证集 → `images/val/` + `masks/val/`
6. 按分布生成测试集 → `images/test/` + `masks/test/` (图片和水印均独立)

### 2. 训练模型

#### 2.1 转换 Mask 为 YOLO 标签 (必须)

训练前必须将 mask 转换为 YOLO 分割标签:

```bash
python script/inpaint/mask_to_yolo_label.py D:/Work/data/inpaint_data/images/train D:/Work/data/inpaint_data/masks/train D:/Work/data/inpaint_data/labels/train 0
python script/inpaint/mask_to_yolo_label.py D:/Work/data/inpaint_data/images/val D:/Work/data/inpaint_data/masks/val D:/Work/data/inpaint_data/labels/val 0
python script/inpaint/mask_to_yolo_label.py D:/Work/data/inpaint_data/images/test D:/Work/data/inpaint_data/masks/test D:/Work/data/inpaint_data/labels/test 0
```

#### 2.2 GPU 训练 (推荐)

**Windows WSL2 + AMD ROCm**:

```bash
# 在 WSL Ubuntu 中激活虚拟环境
source ~/rocm_env/bin/activate

# 使用训练脚本 (推荐)
cd /mnt/d/Work/github/avox
bash script/inpaint/train_yolo_seg.sh base          # 从头训练
bash script/inpaint/train_yolo_seg.sh base resume   # 断点续训

# 或直接运行命令
yolo segment train \
  data=/mnt/d/Work/data/inpaint_data/dataset.yaml \
  model=/mnt/d/Work/data/inpaint_data/runs/watermark_seg_v2/weights/best.pt \
  epochs=50 batch=8 imgsz=640 device=0 \
  project=/mnt/d/Work/data/inpaint_data/runs \
  name=watermark_seg_v3 exist_ok=True save_period=5
```

训练环境信息:
- 虚拟环境路径: `~/rocm_env/` (venv)
- 设备: AMD GPU (ROCm)
- Batch: 8 (默认)
- 每 5 个 epoch 保存检查点

Linux/macOS 使用训练脚本:

```bash
# 支持: macOS (MPS), WSL2+ROCm, Linux+CUDA
bash script/inpaint/train_yolo_seg.sh        # 默认 base，自动选择 batch
bash script/inpaint/train_yolo_seg.sh mini   # 快速模型
bash script/inpaint/train_yolo_seg.sh high   # 高精度模型
bash script/inpaint/train_yolo_seg.sh base resume  # 断点续训
```

训练特性:
- **自动 batch**: 根据 GPU 显存自动选择 batch (2-32)
- **多平台**: 自动检测 macOS (MPS) / Linux (CUDA/ROCm) / WSL2
- **每 5 个 epoch** 自动保存检查点
- **支持断点续训** (`resume` 参数)

#### 2.3 CPU 训练 (无GPU时)

```bash
yolo segment train data=D:/Work/data/inpaint_data/dataset.yaml model=yolo11n-seg.pt epochs=50 imgsz=640 batch=8 device=cpu project=D:/Work/data/inpaint_data/runs name=watermark_seg_cpu
```

### 2.2 导出 ONNX 模型

训练完成后，导出 ONNX 用于 C++ 推理：

```bash
# 训练输出目录: D:\Work\data\inpaint_data\runs\watermark_seg_base\weights\best.pt

python -c "
from ultralytics import YOLO
model = YOLO(r'D:\Work\data\inpaint_data\runs\watermark_seg_base\weights\best.pt')
model.export(format='onnx', opset=12, simplify=True, imgsz=640)
"
```

复制到项目目录：
```bash
cp D:\best.onnx D:\Work\github\avox\assets\models\inpaint\yolo26m-seg_watermark.onnx
```

### 3. 对比测试 (真实 mask vs YOLO mask)

```bash
# 随机选10张图，对比真实mask和YOLO检测mask的修复效果
python script/inpaint/compare_gt_yolo.py
```

测试流程:
1. **自动清空** `result/` 目录
2. 随机选择 10 张有真实 mask 的图片
3. 分别运行 YOLO 检测 mask 和真实 mask 进行 LaMa 修复
4. 生成组合对比图

输出文件:
- `*_compare.jpg` - 六合一组合对比图

组合图布局:
```
上排: 原图 | YOLO Mask | YOLO 修复结果
下排: 原图 | GT Mask   | GT 修复结果
```

### 4. 单张图片测试

```bash
# 编译后运行
inpainttest.exe -i test.png                    # 自动检测+修复
inpainttest.exe -i test.png -m                 # 只检测，保存 mask 图片
inpainttest.exe -i test.png -r "100,200,50,30" # 手动指定区域
inpainttest.exe -i test.png -g                 # 使用GPU
inpainttest.exe -i test.png -l 2               # 使用 high 模型
inpainttest.exe -i test.png -v                 # 详细输出
```

---

## 脚本说明

| 脚本 | 功能 |
|------|------|
| `generate_watermark_dataset.py` | 一站式生成数据集 (下载+生成) |
| `mask_to_yolo_label.py` | 将二值掩码转换为 YOLO 分割标签 |
| `train_yolo_seg.sh` | 训练 YOLO26-Seg 模型 (支持多平台,自动 batch) |
| `download_yolo26_seg.py` | 下载 YOLO26-Seg 预训练模型 |
| `download_lama_models.py` | 下载 LaMa 模型 |
| `compare_gt_yolo.py` | 对比真实 mask vs YOLO mask 修复效果 |

### generate_watermark_dataset.py

一站式数据集生成脚本:

```bash
python script/inpaint/generate_watermark_dataset.py
```

功能:
- 从 URL 下载原始图片和水印
- 生成文字水印模板 (含中文，动态组合)
- 按配置生成训练/验证/测试集
- 生成 `dataset.yaml` 配置文件

配置:
- 训练集: 5,000 张
- 验证集: 800 张
- 测试集: 800 张 (图片和水印独立)
- 类型分布: 文字 50%, 非文字 50%
- 透明度: 0.0-0.8 均匀分布
- 文字水印从词汇库动态生成，覆盖 7000+ 种可能

### mask_to_yolo_label.py

将二值掩码 PNG 转换为 YOLO 分割格式的多边形坐标 (多线程):

```bash
python script/inpaint/mask_to_yolo_label.py <images_dir> <masks_dir> <labels_dir> [class_id] [workers]
# 示例:
python script/inpaint/mask_to_yolo_label.py D:/Work/data/inpaint_data/images/train D:/Work/data/inpaint_data/masks/train D:/Work/data/inpaint_data/labels/train 0 8
```

### train_yolo_seg.sh

```bash
bash script/inpaint/train_yolo_seg.sh [mini|base|high] [resume]
```

---

## API

### C++ 接口 (IWatermarkRemoval 纯接口)

```cpp
#include "avox/AvoxVision.h"
#include "avox/module/AvoxManager.hpp"
#include "avox/module/ModuleMgr.hpp"

// 初始化 (确保插件已加载)
ModuleMgr::Get().ensureStarted();

// 创建实例
IWatermarkRemoval* raw = AvoxManager::Get().watermarkRemovalHub.create("inpaint");
std::unique_ptr<IWatermarkRemoval> inpaint(raw);

// 配置
inpaint->setModelLevel(ModelLevel::base);  // mini/base/high
inpaint->setInpaintMode(InpaintMode::lama);  // lama/aotgan
inpaint->setUseGPU(false);
inpaint->setDetectThreshold(0.15f);
inpaint->setMaskDilate(20);

// 加载模型 (按配置选模型经缓存加载)
inpaint->open();

// 自动检测+修复
inpaint->process(inputBuf, outputBuf);

// 仅检测
inpaint->detect(inputBuf, maskBuf);

// 使用指定 mask 修复
inpaint->inpaint(inputBuf, maskBuf, outputBuf);

// 获取结果
int count = inpaint->getWatermarkCount();
float x, y, w, h;
inpaint->getWatermarkBBox(0, &x, &y, &w, &h);
float detectMs = inpaint->getDetectTimeMs();
float inpaintMs = inpaint->getInpaintTimeMs();

// 释放 (unique_ptr 自动 delete)
```

### 检测结果格式

```json
{
  "watermarks": [
    {"x": 100, "y": 200, "w": 50, "h": 30, "conf": 0.85, "type": 0}
  ],
  "count": 1,
  "success": true
}
```

---

## 更新日志

### 2026-04-12
- 数据集配置更新: 训练集 5000 张，文字水印占比 50%
- 新增 `watermark_vocabulary.txt` 词汇库，动态组合生成 7000+ 种文字水印
- 新增 `mask_to_yolo_label.py` 多线程标签转换脚本
- 训练环境说明: Windows 使用 `rocm_env` (AMD ROCm GPU)

### 2026-04-11
- 新增 `generate_watermark_dataset.py` 一站式数据生成脚本
- 添加独立测试集 (图片和水印均独立于训练集)
- 类型分布优化: 文字 40% (含中文 30%), 非文字 60%
- 透明度均匀分布 0.0-0.8
- 修复 compare_gt_yolo.py YOLO mask 膨胀处理

### 2026-04
- 升级到 YOLO26-Seg 实例分割模型
- 使用精确分割掩码替代边界框
- 支持 ModelLevel 切换模型大小 (mini/base/high)
- 支持断点续训和多平台训练 (macOS/Linux/Windows)
