# AI 图像修复模块

图像修复模块提供基于 AI 的水印去除、物体移除功能，全自动检查并修复。

## 方案演进过程

### 检测：从传统算法到 YOLO-Seg

最初使用帧差分 + 边缘分析的传统方法检测水印，但对半透明水印、静态图片场景效果不佳。后来切换到 **YOLO26-Seg** 实例分割模型，输出精确像素级分割掩码，大幅提升检测精度。

### 修复：从 OpenCV Inpaint 到 LaMa

初期使用 OpenCV 的 `cv::inpaint` 算法，但对复杂背景、大面积水印修复效果有限。后迁移到 **LaMa / AOT-GAN** 深度学习方案，修复质量显著提升。

| 方案 | 效果 | 适用场景 |
|------|------|----------|
| OpenCV Inpaint | 一般 | 简单背景、小面积 |
| LaMa | 优秀 | 复杂背景、大面积 |
| AOT-GAN | 良好 | 追求速度 |

---

## 训练流程

### 数据集生成

训练数据质量直接决定 YOLO 检测器的泛化能力。初期纯文本及带透明的水印检测效果差，原因是样式单一、缺乏多样性。现采用**多维度随机化**策略生成水印，覆盖各种可能的样式组合。

#### 文本内容随机化

从词汇库动态组合生成，覆盖 7000+ 种可能：

| 类型 | 生成策略 | 示例 |
|------|----------|------|
| text_short | 状态词 / 中文组合 | "HD"、"PREVIEW"、"仅供预览" |
| text_long | 模板 + 变量替换 | "NOT FOR {noun} ONLY" |
| text_chinese | 中文模板组合 | "{action}{noun}使用" |
| copyright | 年份 + 公司名称 | "© 2025 Company" |

词汇库 (`watermark_vocabulary.txt`) 包含：
- 动作词 (action): DISTRIBUTION, RESALE, PREVIEW...
- 名词 (noun): PERSONAL USE, INTERNAL, MEMBERS...
- 状态词 (status): HD, VIP, PREVIEW, MEMBER...
- 中文词汇: 仅供、预览、会员、内部...

#### 文本样式随机化

每个文本水印独立随机以下属性：

| 属性 | 范围 | 说明 |
|------|------|------|
| 字体 | 10+ 种 | Arial, Times, Georgia, 微软雅黑, 黑体, 宋体... |
| 字号 | 25-120px | 短文本更大，长文本更小 |
| 颜色 | 8 种 | 白、黑、红、黄、蓝、灰、橙、绿 |
| 效果 | 4 种 | shadow, outline, glow, none |
| 透明度 | 0.0-0.8 | 均匀分布，模拟真实水印 |

#### 位置与变换随机化

| 属性 | 范围 | 说明 |
|------|------|------|
| 缩放 | 0.4-4.0x | 相对于基准尺寸 |
| 旋转 | -30° ~ +30° | 随机旋转角度 |
| 位置 | 全图 + 边界外溢 | 允许 40% 超出边界，模拟裁切水印 |

#### 合成流程

```
原始图片 + 水印图层 ──> 随机缩放 ──> 随机旋转 ──> 随机位置 ──> 透明度叠加 ──> 带水印图片 + 分割掩码
```

#### 数据集划分

| 数据集 | 数量 | 图片来源 | 水印来源 |
|--------|------|----------|----------|
| 训练集 | 5,000 | train_val | A组 |
| 验证集 | 800 | train_val | A组 |
| 测试集 | 800 | test (独立) | B组 (独立) |

#### 生成命令

```bash
# 一站式生成: 下载图片 + 生成水印 + 生成数据集
python script/inpaint/generate_watermark_dataset.py
```

### AMD GPU 训练 (ROCm)

家中只有 AMD 显卡，CPU 训练太慢（GPU 一天，CPU 要五天）。Windows 的 ROCm 支持不好，因此使用 WSL2 环境训练。

**注意**：ROCm + 显卡 + 驱动三者版本必须匹配安装，这个安装过程花了几晚上才把 WSL2 + ROCm + 9070XT 跑起来。

主要踩坑点：
- **显卡代数支持**：ROCm 对新显卡支持较慢，9070XT 需要最新版本 ROCm（当前最新 7.2.x）
- **驱动兼容性**：Windows 主机驱动和 WSL2 内的 ROCm 版本要配套，版本不匹配会导致 GPU 不可识别
- **内核版本**：WSL2 Linux 内核版本影响 ROCm 支持，可能需要更新 wsl 内核
- **环境变量**：安装后需要正确配置 `HSA_OVERRIDE_GFX_VERSION` 等环境变量

建议：
1. 先确认显卡型号对应的 ROCm 最低版本要求
2. 按官方文档顺序安装：主机驱动 → WSL2 → ROCm
3. 用 `rocminfo` 和 `hipconfig` 验证安装是否成功
4. 遇到问题先查 ROCm 官方 Issue 和社区讨论

#### 环境准备

```bash
# 在 WSL Ubuntu 中创建虚拟环境
python -m venv ~/rocm_env
source ~/rocm_env/bin/activate

# 安装依赖
pip install ultralytics opencv-python
```

#### 训练命令

```bash
# 进入项目目录
cd /mnt/d/Work/github/avox

# 从头训练
bash script/inpaint/train_yolo_seg.sh base

# 断点续训
bash script/inpaint/train_yolo_seg.sh base resume
```

训练参数：
- 设备: AMD GPU (ROCm)
- Batch: 8 (高了各种问题)
- 每 5 个 epoch 保存检查点
- 支持断点续训

#### Mask 转 YOLO 标签

训练前需要将 mask 转换为 YOLO 分割标签：

```bash
python script/inpaint/mask_to_yolo_label.py D:/Work/data/inpaint_data/images/train D:/Work/data/inpaint_data/masks/train D:/Work/data/inpaint_data/labels/train 0
python script/inpaint/mask_to_yolo_label.py D:/Work/data/inpaint_data/images/val D:/Work/data/inpaint_data/masks/val D:/Work/data/inpaint_data/labels/val 0
python script/inpaint/mask_to_yolo_label.py D:/Work/data/inpaint_data/images/test D:/Work/data/inpaint_data/masks/test D:/Work/data/inpaint_data/labels/test 0
```

---

## 核心实现

### 双模型架构

| 组件 | 模型 | 功能 | 输出 |
|------|------|------|------|
| 检测 | YOLO26-Seg | 水印定位 | 精确分割掩码 |
| 修复 | LaMa / AOT-GAN | 图像填充 | 修复后图像 |

### 数据流

```
输入图像 ──> YOLO 检测 ──> 分割掩码 ──> Mask 膨胀 ──> LaMa/AOT-GAN 修复 ──> 输出图像
```

**关键步骤**：
- **YOLO-Seg 检测**：输出像素级掩码而非边界框，边缘更精确
- **Mask 膨胀**：扩大修复区域，解决边缘残留问题
- **分块修复**：TiledInpainter 处理大分辨率图像

### 模型级别

**YOLO 检测模型**：

| ModelLevel | 模型文件 | 速度 | 适用场景 |
|------------|----------|------|----------|
| mini | yolo26n-seg | 最快 | 实时视频处理 |
| base | yolo26m-seg | 中等 | 通用场景 (推荐) |
| high | yolo26x-seg | 最慢 | 高精度需求 |

**修复模型**：

| 模型 | 大小 | 特点 |
|------|------|------|
| LaMa | 198 MB | 修复质量高 (默认) |
| AOT-GAN | ~50 MB | 速度更快 |

---

## 关键问题解决

### 细小水印修复效果差

**问题**：水印边缘只有 1-2 像素时，YOLO 检测的 mask 边界不够准确，导致修复后边缘残留。

**解决**：对 mask 进行膨胀处理 (`cv::dilate`)，扩大修复区域。默认膨胀核 5x5，迭代 3 次。

```cpp
// LamaInpainter.cpp
cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(5, 5));
cv::dilate(maskMat, dilated, kernel, cv::Point(-1, -1), 3);
```

### 大分辨率图像修复

**问题**：LaMa 模型输入固定 512x512，大分辨率图像无法直接处理。

**解决**：TiledInpainter 分块修复 + 加权融合。

```
原图 (4K) ──> 分割成 512x512 块 ──> 逐块修复 ──> 加权融合 ──> 输出 (4K)
```

分块策略：
- 分块大小 512x512，重叠区域 32 像素
- 融合时使用距离权重，避免块边界痕迹

---

## 代码实现

### 检测流程

```cpp
// YOLO 检测水印，返回分割掩码
std::vector<WatermarkSeg> YOLODetector::detectWithMask(const uint8_t* rgbImage,
                                                        int width, int height,
                                                        float confThreshold,
                                                        float nmsThreshold) {
    // 1. 预处理: letterbox resize + 归一化
    letterbox(rgbImage, width, height, scaleX, scaleY);

    // 2. ONNX 推理
    session.run();

    // 3. 后处理: 解析检测输出 + 分割掩码
    return postprocessSeg(detOutput, protoOutput, ...);
}
```

### 修复流程

```cpp
// LaMa 图像修复
bool LamaInpainter::inpaint(const uint8_t* rgbImage,
                             const uint8_t* mask,
                             int width, int height,
                             uint8_t* output) {
    // 1. Mask 膨胀
    dilateMask(mask, width, height);

    // 2. 图像和 mask resize 到 512x512
    cv::resize(imgMat, imgResized, cv::Size(512, 512), 0, 0, cv::INTER_LANCZOS4);
    cv::resize(dilated, maskResized, cv::Size(512, 512), 0, 0, cv::INTER_NEAREST);

    // 3. 归一化到 [0, 1]
    preprocess(imgResized, maskResized);

    // 4. ONNX 推理
    session.run();

    // 5. 后处理: resize 回原尺寸
    postprocess(output, original, mask);
}
```